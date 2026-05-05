/**
 * @file split_link.cpp
 * @brief C3-Cascade — ESP-NOW split keyboard communication with auto-discovery
 *
 * ESP-NOW transport implementation:
 *   - Auto-discovery: slaves broadcast DISCOVER, master responds with ACK
 *   - No hardcoded MAC addresses needed (optional override still supported)
 *   - Master registers slave peers dynamically on discovery
 *   - Slave sends matrix report to master on every state change
 *   - Master sends periodic heartbeats to all discovered slaves
 *
 * ESP-NOW coexists with BLE on the same 2.4GHz radio.
 * Both use NimBLE-managed radio time-sharing on ESP32-C3/C6.
 */

#include <Arduino.h>
#include <string.h>

#include "split_link.h"
#include "config.h"
#include "pins.h"
#include "hal/hal.h"

#if SPLIT_TRANSPORT_ESPNOW

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ============================================================================
// Internal state
// ============================================================================

// Slave info array (master tracks these)
static slave_info_t slaves[MAX_SLAVE_NODES];
static uint8_t      num_slaves = 0;

// Receive buffer (written from callback, read from main loop)
static volatile bool        rx_pending = false;
static split_packet_t       rx_packet;
// Store sender MAC from callback (ESP-NOW provides this separately)
static uint8_t              rx_sender_mac[6];

// Discovery state (slave)
static bool     discovery_complete = false;
static uint8_t  master_mac[6] = {0};
static uint8_t  my_slave_id = 0;
static uint32_t last_discover_time = 0;

// Heartbeat timer (master only)
static uint32_t last_heartbeat_time = 0;

// ESP-NOW broadcast address
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================================
// ESP-NOW Callbacks
// ============================================================================

/**
 * @brief Called when data is received via ESP-NOW
 */
static void on_data_recv(const uint8_t *mac_addr,
                         const uint8_t *data, int data_len) {
    if (data_len != sizeof(split_packet_t)) {
        DBG_PRINT("[ESPNOW] Unexpected packet size: %d\n", data_len);
        return;
    }

    memcpy((void*)&rx_packet, data, sizeof(split_packet_t));
    memcpy(rx_sender_mac, mac_addr, 6);
    rx_pending = true;

    #if DEBUG_ESPNOW
    DBG_PRINT("[ESPNOW] RX from %02X:%02X:%02X:%02X:%02X:%02X type=0x%02X\n",
              mac_addr[0], mac_addr[1], mac_addr[2],
              mac_addr[3], mac_addr[4], mac_addr[5],
              rx_packet.type);
    #endif
}

/**
 * @brief Called when data send is complete
 */
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    #if DEBUG_ESPNOW
    DBG_PRINT("[ESPNOW] TX to %02X:%02X:%02X:%02X:%02X:%02X status=%s\n",
              mac_addr[0], mac_addr[1], mac_addr[2],
              mac_addr[3], mac_addr[4], mac_addr[5],
              status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
    #endif
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * @brief Register a peer by MAC address
 */
static bool add_peer(const uint8_t* mac) {
    // Check if already registered
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;

    esp_err_t result = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        DBG_PRINT("[ESPNOW] Failed to add peer: %d\n", result);
        return false;
    }

    DBG_PRINT("[ESPNOW] Peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

/**
 * @brief Check if a MAC matches a slave we already know
 * @return slave index, or -1 if not found
 */
static int find_slave_by_mac(const uint8_t* mac) {
    for (uint8_t i = 0; i < num_slaves; i++) {
        if (memcmp(slaves[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Initialization
// ============================================================================

void split_link_init() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    DBG_PRINTLN(F("[ESPNOW] Initializing ESP-NOW split link (auto-discovery)..."));

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        DBG_PRINTLN(F("[ESPNOW] ERROR: esp_now_init() failed!"));
        return;
    }

    // Register callbacks
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    // Add broadcast peer (needed for discovery broadcasts)
    add_peer(BROADCAST_MAC);

    #if IS_MASTER
    // Master: initialize slave array, wait for discovery
    num_slaves = 0;
    for (uint8_t i = 0; i < MAX_SLAVE_NODES; i++) {
        memset(&slaves[i], 0, sizeof(slave_info_t));
    }
    discovery_complete = false;
    DBG_PRINTLN(F("[ESPNOW] Master: waiting for slave discovery broadcasts..."));

    #else
    // Slave: begin discovery — will broadcast DISCOVER packets
    discovery_complete = false;
    last_discover_time = 0;
    DBG_PRINTLN(F("[ESPNOW] Slave: starting auto-discovery..."));
    #endif
}

// ============================================================================
// Master: Discovery + Update
// ============================================================================

#if IS_MASTER
/**
 * @brief Master: handle a DISCOVER packet from a slave
 */
static void master_handle_discover(const uint8_t* sender_mac, const split_packet_t* pkt) {
    // Check if we already know this slave
    int idx = find_slave_by_mac(sender_mac);

    if (idx < 0) {
        // New slave — register it
        if (num_slaves >= MAX_SLAVE_NODES) {
            DBG_PRINTLN(F("[ESPNOW] Max slaves reached, ignoring discovery"));
            return;
        }

        idx = num_slaves;
        num_slaves++;

        memcpy(slaves[idx].mac, sender_mac, 6);
        memset(&slaves[idx].matrix, 0, sizeof(matrix_state_t));
        slaves[idx].last_seen = hal::millis_now();
        slaves[idx].battery_level = 0xFF;
        slaves[idx].connected = false;
        slaves[idx].discovered = true;

        // Register as ESP-NOW peer
        add_peer(sender_mac);

        DBG_PRINT("[ESPNOW] Discovered slave %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  idx,
                  sender_mac[0], sender_mac[1], sender_mac[2],
                  sender_mac[3], sender_mac[4], sender_mac[5]);
    }

    // Send DISCOVER_ACK with assigned slave_id
    split_packet_t ack = {};
    ack.type = SPLIT_PKT_DISCOVER_ACK;
    ack.slave_id = (uint8_t)idx;
    ack.timestamp = hal::millis_now();
    hal::get_mac_address(ack.mac);  // Include master MAC

    esp_now_send(sender_mac, (uint8_t*)&ack, sizeof(ack));

    DBG_PRINT("[ESPNOW] Sent DISCOVER_ACK to slave %d\n", idx);
    discovery_complete = true;
}

/**
 * @brief Master: process received data
 */
static void master_process_rx() {
    if (!rx_pending) return;
    rx_pending = false;

    if (rx_packet.type == SPLIT_PKT_DISCOVER) {
        master_handle_discover(rx_sender_mac, &rx_packet);

    } else if (rx_packet.type == SPLIT_PKT_MATRIX_REPORT) {
        uint8_t sid = rx_packet.slave_id;
        if (sid < num_slaves) {
            for (int r = 0; r < MATRIX_ROWS; r++) {
                slaves[sid].matrix.rows[r] = rx_packet.matrix[r];
            }
            slaves[sid].last_seen = hal::millis_now();
            slaves[sid].battery_level = rx_packet.battery_level;
            slaves[sid].connected = true;

            #if DEBUG_ESPNOW
            DBG_PRINT("[ESPNOW] Slave %d matrix updated\n", sid);
            #endif
        }
    }
}

/**
 * @brief Master: send heartbeat to all discovered slaves
 */
static void master_send_heartbeat() {
    uint32_t now = hal::millis_now();
    if ((now - last_heartbeat_time) < ESPNOW_HEARTBEAT_MS) return;
    last_heartbeat_time = now;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_HEARTBEAT;
    pkt.slave_id = 0xFF;  // broadcast
    pkt.timestamp = now;

    for (uint8_t i = 0; i < num_slaves; i++) {
        esp_now_send(slaves[i].mac, (uint8_t*)&pkt, sizeof(pkt));

        // Check for slave timeout
        if (slaves[i].connected &&
            (now - slaves[i].last_seen) > ESPNOW_TIMEOUT_MS) {
            slaves[i].connected = false;
            memset(&slaves[i].matrix, 0, sizeof(matrix_state_t));
            DBG_PRINT("[ESPNOW] Slave %d timed out\n", i);
        }
    }
}
#endif // IS_MASTER

// ============================================================================
// Slave: Discovery + Update
// ============================================================================

#if IS_SLAVE
/**
 * @brief Slave: send a discovery broadcast
 */
static void slave_send_discover() {
    if (discovery_complete) return;

    uint32_t now = hal::millis_now();
    if ((now - last_discover_time) < SPLIT_DISCOVER_INTERVAL_MS) return;
    last_discover_time = now;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_DISCOVER;
    pkt.slave_id = 0xFF;  // Unknown yet
    pkt.timestamp = now;
    hal::get_mac_address(pkt.mac);  // Include our MAC

    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));

    DBG_PRINTLN(F("[ESPNOW] Sent DISCOVER broadcast"));
}

/**
 * @brief Slave: process received master data
 */
static void slave_process_rx() {
    if (!rx_pending) return;
    rx_pending = false;

    if (rx_packet.type == SPLIT_PKT_DISCOVER_ACK) {
        // Master acknowledged us!
        my_slave_id = rx_packet.slave_id;
        memcpy(master_mac, rx_packet.mac, 6);
        discovery_complete = true;

        // Register master as unicast peer
        add_peer(master_mac);

        DBG_PRINT("[ESPNOW] Paired with master! Assigned ID: %d\n", my_slave_id);
        DBG_PRINT("[ESPNOW] Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  master_mac[0], master_mac[1], master_mac[2],
                  master_mac[3], master_mac[4], master_mac[5]);

    } else if (rx_packet.type == SPLIT_PKT_HEARTBEAT) {
        #if DEBUG_ESPNOW
        DBG_PRINTLN(F("[ESPNOW] Heartbeat received from master"));
        #endif
    } else if (rx_packet.type == SPLIT_PKT_SYNC) {
        #if DEBUG_ESPNOW
        DBG_PRINTLN(F("[ESPNOW] Sync command from master"));
        #endif
        // Future: handle sync (layer change, LED state, etc.)
    }
}
#endif // IS_SLAVE

// ============================================================================
// Update (call each loop iteration)
// ============================================================================

void split_link_update() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    #if IS_MASTER
    master_process_rx();
    if (num_slaves > 0) {
        master_send_heartbeat();
    }
    #else
    slave_send_discover();
    slave_process_rx();
    #endif
}

// ============================================================================
// Data API
// ============================================================================

void split_link_send_matrix(const matrix_state_t* state) {
    #if !ENABLE_SPLIT_LINK || !IS_SLAVE
    (void)state;
    return;
    #endif

    #if IS_SLAVE
    if (!discovery_complete) return;  // Don't send until paired

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_MATRIX_REPORT;
    pkt.slave_id = my_slave_id;
    pkt.timestamp = hal::millis_now();
    pkt.battery_level = 0xFF;  // Unknown

    for (int r = 0; r < MATRIX_ROWS; r++) {
        pkt.matrix[r] = state->rows[r];
    }

    esp_now_send(master_mac, (uint8_t*)&pkt, sizeof(pkt));

    #if DEBUG_ESPNOW
    DBG_PRINTLN(F("[ESPNOW] Matrix report sent to master"));
    #endif
    #endif
}

const matrix_state_t* split_link_get_slave_matrix(uint8_t slave_id) {
    #if IS_MASTER
    if (slave_id < num_slaves && slaves[slave_id].connected) {
        return &slaves[slave_id].matrix;
    }
    #endif
    (void)slave_id;
    return nullptr;
}

bool split_link_slave_connected(uint8_t slave_id) {
    #if IS_MASTER
    if (slave_id < num_slaves) {
        return slaves[slave_id].connected;
    }
    #endif
    (void)slave_id;
    return false;
}

uint8_t split_link_slave_count() {
    return num_slaves;
}

bool split_link_is_paired() {
    return discovery_complete;
}

void split_link_shutdown() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    DBG_PRINTLN(F("[ESPNOW] Shutting down..."));
    esp_now_deinit();
    DBG_PRINTLN(F("[ESPNOW] Shutdown complete"));
}

#elif SPLIT_TRANSPORT_WIFI_UDP

// WiFi UDP transport is implemented in split_link_wifi_udp.cpp

#else // No transport available (nRF52840 or unsupported)

// ============================================================================
// Stub implementation for platforms without wireless split
// ============================================================================

void split_link_init() {
    DBG_PRINTLN(F("[SPLIT] No wireless transport available on this platform"));
    // TODO: Implement BLE-based split link for nRF52840
}

void split_link_update() {}

void split_link_send_matrix(const matrix_state_t* state) {
    (void)state;
}

const matrix_state_t* split_link_get_slave_matrix(uint8_t slave_id) {
    (void)slave_id;
    return nullptr;
}

bool split_link_slave_connected(uint8_t slave_id) {
    (void)slave_id;
    return false;
}

uint8_t split_link_slave_count() {
    return 0;
}

bool split_link_is_paired() {
    return false;
}

void split_link_shutdown() {}

#endif // Transport selection
