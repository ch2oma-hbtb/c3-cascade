/**
 * @file split_link.cpp
 * @brief C3-Cascade — ESP-NOW split keyboard communication implementation
 *
 * ESP-NOW protocol:
 *   - Master registers slave MAC addresses as peers
 *   - Master listens for matrix reports from slaves
 *   - Master sends periodic heartbeats to slaves
 *   - Slave sends matrix report to master on every state change
 *   - Slave listens for heartbeats/sync from master
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

#if HAS_ESPNOW

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

// Master MAC address (used by slaves)
static uint8_t master_mac[] = MASTER_MAC;

// Slave MAC addresses (used by master)
static uint8_t slave_macs[][6] = {
    SLAVE_MAC_1,
    #if MAX_SLAVE_NODES > 1
    SLAVE_MAC_2,
    #endif
};

// Heartbeat timer (master only)
static uint32_t last_heartbeat_time = 0;

// ============================================================================
// ESP-NOW Callbacks
// ============================================================================

/**
 * @brief Called when data is received via ESP-NOW
 * Note: ESP-IDF 4.4.x callback signature (used by arduino-esp32 2.0.x)
 */
static void on_data_recv(const uint8_t *mac_addr,
                         const uint8_t *data, int data_len) {
    if (data_len != sizeof(split_packet_t)) {
        DBG_PRINT("[ESPNOW] Unexpected packet size: %d\n", data_len);
        return;
    }

    memcpy((void*)&rx_packet, data, sizeof(split_packet_t));
    rx_pending = true;

    #if DEBUG_ESPNOW
    DBG_PRINT("[ESPNOW] RX from %02X:%02X:%02X:%02X:%02X:%02X type=%d\n",
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
// Initialization
// ============================================================================

/**
 * @brief Register a peer by MAC address
 */
static bool add_peer(const uint8_t* mac) {
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;  // No encryption (can be enabled later)

    esp_err_t result = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        DBG_PRINT("[ESPNOW] Failed to add peer: %d\n", result);
        return false;
    }

    DBG_PRINT("[ESPNOW] Peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

void split_link_init() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    DBG_PRINTLN(F("[ESPNOW] Initializing ESP-NOW split link..."));

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        DBG_PRINTLN(F("[ESPNOW] ERROR: esp_now_init() failed!"));
        return;
    }

    // Register callbacks
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    #if IS_MASTER
    // Master: register all slave peers
    num_slaves = sizeof(slave_macs) / sizeof(slave_macs[0]);
    if (num_slaves > MAX_SLAVE_NODES) num_slaves = MAX_SLAVE_NODES;

    for (uint8_t i = 0; i < num_slaves; i++) {
        add_peer(slave_macs[i]);

        // Initialize slave info
        memcpy(slaves[i].mac, slave_macs[i], 6);
        memset(&slaves[i].matrix, 0, sizeof(matrix_state_t));
        slaves[i].last_seen = 0;
        slaves[i].battery_level = 0xFF;
        slaves[i].connected = false;
    }

    DBG_PRINT("[ESPNOW] Master initialized with %d slave(s)\n", num_slaves);

    #else
    // Slave: register master as peer
    add_peer(master_mac);
    DBG_PRINTLN(F("[ESPNOW] Slave initialized, master peer registered"));
    #endif
}

// ============================================================================
// Update (call each loop iteration)
// ============================================================================

#if IS_MASTER
/**
 * @brief Master: process received slave data
 */
static void master_process_rx() {
    if (!rx_pending) return;
    rx_pending = false;

    if (rx_packet.type == SPLIT_PKT_MATRIX_REPORT) {
        uint8_t sid = rx_packet.slave_id;
        if (sid < num_slaves) {
            // Update slave matrix state
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
 * @brief Master: send heartbeat to all slaves
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

#if IS_SLAVE
/**
 * @brief Slave: process received master data
 */
static void slave_process_rx() {
    if (!rx_pending) return;
    rx_pending = false;

    if (rx_packet.type == SPLIT_PKT_HEARTBEAT) {
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

void split_link_update() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    #if IS_MASTER
    master_process_rx();
    master_send_heartbeat();
    #else
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
    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_MATRIX_REPORT;
    pkt.slave_id = 0;  // TODO: make configurable for multi-slave
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

void split_link_shutdown() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    DBG_PRINTLN(F("[ESPNOW] Shutting down..."));
    esp_now_deinit();
    DBG_PRINTLN(F("[ESPNOW] Shutdown complete"));
}

#else // !HAS_ESPNOW (nRF52840 or unsupported)

// ============================================================================
// Stub implementation for non-ESP-NOW platforms
// ============================================================================

void split_link_init() {
    DBG_PRINTLN(F("[SPLIT] ESP-NOW not available on this platform"));
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

void split_link_shutdown() {}

#endif // HAS_ESPNOW
