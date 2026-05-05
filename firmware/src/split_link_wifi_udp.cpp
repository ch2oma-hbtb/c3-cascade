/**
 * @file split_link_wifi_udp.cpp
 * @brief C3-Cascade — WiFi UDP split keyboard communication with auto-discovery
 *
 * WiFi UDP transport for boards without ESP-NOW (e.g., Pico 2W):
 *   - Master creates a SoftAP with SSID "C3C-XXXX" (last 4 hex of MAC)
 *   - Slave scans for SSIDs matching "C3C-" prefix and connects
 *   - Discovery via UDP broadcast on port 4200
 *   - Matrix reports sent as UDP unicast after pairing
 *
 * Also works on ESP32 boards when compiled with -DFORCE_WIFI_UDP=1
 * (useful for mixed ESP32 + Pico W setups).
 */

#include <Arduino.h>
#include <string.h>

#include "split_link.h"
#include "config.h"
#include "pins.h"
#include "hal/hal.h"

#if SPLIT_TRANSPORT_WIFI_UDP

#include <WiFi.h>
#include <WiFiUdp.h>

// ============================================================================
// Internal state
// ============================================================================

// Slave info array (master tracks these)
static slave_info_t slaves[MAX_SLAVE_NODES];
static uint8_t      num_slaves = 0;

// WiFi UDP socket
static WiFiUDP udp;

// Discovery state
static bool     discovery_complete = false;
static uint32_t last_discover_time = 0;

// Slave-specific: master's IP once connected
static IPAddress master_ip;
static uint8_t   my_slave_id = 0;

// Master-specific: slave IPs for unicast
static IPAddress slave_ips[MAX_SLAVE_NODES];

// Heartbeat timer (master only)
static uint32_t last_heartbeat_time = 0;

// SoftAP SSID (built at runtime from MAC)
static char ap_ssid[32] = {0};

// ============================================================================
// Helpers
// ============================================================================

/**
 * @brief Build the SoftAP SSID from the board's MAC address
 * Format: "C3C-XXXX" where XXXX is the last 2 bytes of MAC in hex
 */
static void build_ap_ssid() {
    uint8_t mac[6];
    hal::get_mac_address(mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X",
             SPLIT_WIFI_AP_PREFIX, mac[4], mac[5]);
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

/**
 * @brief Send a split packet via UDP to a specific IP
 */
static void udp_send_packet(const split_packet_t* pkt, IPAddress ip, uint16_t port) {
    udp.beginPacket(ip, port);
    udp.write((const uint8_t*)pkt, sizeof(split_packet_t));
    udp.endPacket();
}

/**
 * @brief Send a split packet as UDP broadcast
 */
static void udp_broadcast_packet(const split_packet_t* pkt) {
    udp.beginPacket(IPAddress(255, 255, 255, 255), SPLIT_WIFI_UDP_PORT);
    udp.write((const uint8_t*)pkt, sizeof(split_packet_t));
    udp.endPacket();
}

// ============================================================================
// Initialization
// ============================================================================

void split_link_init() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    DBG_PRINTLN(F("[WIFI-UDP] Initializing WiFi UDP split link (auto-discovery)..."));

    #if IS_MASTER
    // ---- Master: Create SoftAP ----
    build_ap_ssid();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, SPLIT_WIFI_PASSWORD);

    // Small delay for AP to stabilize
    delay(100);

    IPAddress ap_ip = WiFi.softAPIP();
    DBG_PRINT("[WIFI-UDP] SoftAP started: SSID=\"%s\" IP=%s\n",
              ap_ssid, ap_ip.toString().c_str());

    // Initialize slave array
    num_slaves = 0;
    for (uint8_t i = 0; i < MAX_SLAVE_NODES; i++) {
        memset(&slaves[i], 0, sizeof(slave_info_t));
    }

    // Start UDP listener
    udp.begin(SPLIT_WIFI_UDP_PORT);
    discovery_complete = false;

    DBG_PRINTLN(F("[WIFI-UDP] Master: listening for slave discovery..."));

    #else
    // ---- Slave: Scan and connect to master's AP ----
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    DBG_PRINTLN(F("[WIFI-UDP] Slave: scanning for master AP..."));

    // Scan for SSIDs matching the C3C- prefix
    bool found = false;
    uint32_t scan_start = hal::millis_now();

    while (!found && (hal::millis_now() - scan_start) < SPLIT_DISCOVER_TIMEOUT_MS) {
        int n = WiFi.scanNetworks();
        DBG_PRINT("[WIFI-UDP] Found %d networks\n", n);

        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            DBG_PRINT("[WIFI-UDP]   %d: %s (RSSI: %d)\n", i, ssid.c_str(), WiFi.RSSI(i));

            if (ssid.startsWith(SPLIT_WIFI_AP_PREFIX)) {
                DBG_PRINT("[WIFI-UDP] Found master AP: %s\n", ssid.c_str());

                WiFi.begin(ssid.c_str(), SPLIT_WIFI_PASSWORD);

                // Wait for connection
                uint32_t connect_start = hal::millis_now();
                while (WiFi.status() != WL_CONNECTED &&
                       (hal::millis_now() - connect_start) < 10000) {
                    delay(100);
                    DBG_PRINT(".");
                }

                if (WiFi.status() == WL_CONNECTED) {
                    found = true;
                    DBG_PRINT("\n[WIFI-UDP] Connected to AP! IP=%s\n",
                              WiFi.localIP().toString().c_str());
                    break;
                } else {
                    DBG_PRINTLN(F("\n[WIFI-UDP] Connection failed, retrying scan..."));
                    WiFi.disconnect();
                }
            }
        }

        WiFi.scanDelete();
        if (!found) {
            delay(1000);  // Wait before re-scanning
        }
    }

    if (!found) {
        DBG_PRINTLN(F("[WIFI-UDP] ERROR: Could not find master AP!"));
        return;
    }

    // Start UDP
    udp.begin(SPLIT_WIFI_UDP_PORT);
    discovery_complete = false;
    last_discover_time = 0;

    DBG_PRINTLN(F("[WIFI-UDP] Slave: starting UDP discovery handshake..."));
    #endif
}

// ============================================================================
// Receive handler (both roles)
// ============================================================================

/**
 * @brief Read one packet from the UDP buffer
 * @return true if a valid packet was received
 */
static bool udp_receive_packet(split_packet_t* pkt, IPAddress* sender_ip) {
    int packetSize = udp.parsePacket();
    if (packetSize != sizeof(split_packet_t)) {
        return false;
    }

    *sender_ip = udp.remoteIP();
    udp.read((uint8_t*)pkt, sizeof(split_packet_t));
    return true;
}

// ============================================================================
// Master: Discovery + Update
// ============================================================================

#if IS_MASTER
/**
 * @brief Master: process all incoming UDP packets
 */
static void master_process_rx() {
    split_packet_t pkt;
    IPAddress sender_ip;

    while (udp_receive_packet(&pkt, &sender_ip)) {
        if (pkt.type == SPLIT_PKT_DISCOVER) {
            // New slave wants to pair
            int idx = find_slave_by_mac(pkt.mac);

            if (idx < 0) {
                // New slave
                if (num_slaves >= MAX_SLAVE_NODES) {
                    DBG_PRINTLN(F("[WIFI-UDP] Max slaves reached, ignoring"));
                    continue;
                }

                idx = num_slaves;
                num_slaves++;

                memcpy(slaves[idx].mac, pkt.mac, 6);
                memset(&slaves[idx].matrix, 0, sizeof(matrix_state_t));
                slaves[idx].last_seen = hal::millis_now();
                slaves[idx].battery_level = 0xFF;
                slaves[idx].connected = false;
                slaves[idx].discovered = true;

                slave_ips[idx] = sender_ip;

                DBG_PRINT("[WIFI-UDP] Discovered slave %d: %02X:%02X:%02X:%02X:%02X:%02X (IP: %s)\n",
                          idx,
                          pkt.mac[0], pkt.mac[1], pkt.mac[2],
                          pkt.mac[3], pkt.mac[4], pkt.mac[5],
                          sender_ip.toString().c_str());
            } else {
                // Known slave reconnecting — update IP
                slave_ips[idx] = sender_ip;
            }

            // Send DISCOVER_ACK
            split_packet_t ack = {};
            ack.type = SPLIT_PKT_DISCOVER_ACK;
            ack.slave_id = (uint8_t)idx;
            ack.timestamp = hal::millis_now();
            hal::get_mac_address(ack.mac);

            udp_send_packet(&ack, sender_ip, SPLIT_WIFI_UDP_PORT);

            DBG_PRINT("[WIFI-UDP] Sent DISCOVER_ACK to slave %d\n", idx);
            discovery_complete = true;

        } else if (pkt.type == SPLIT_PKT_MATRIX_REPORT) {
            uint8_t sid = pkt.slave_id;
            if (sid < num_slaves) {
                for (int r = 0; r < MATRIX_ROWS; r++) {
                    slaves[sid].matrix.rows[r] = pkt.matrix[r];
                }
                slaves[sid].last_seen = hal::millis_now();
                slaves[sid].battery_level = pkt.battery_level;
                slaves[sid].connected = true;

                // Update IP in case it changed
                slave_ips[sid] = sender_ip;

                #if DEBUG_ESPNOW
                DBG_PRINT("[WIFI-UDP] Slave %d matrix updated\n", sid);
                #endif
            }
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
    pkt.slave_id = 0xFF;
    pkt.timestamp = now;

    for (uint8_t i = 0; i < num_slaves; i++) {
        udp_send_packet(&pkt, slave_ips[i], SPLIT_WIFI_UDP_PORT);

        // Check for slave timeout
        if (slaves[i].connected &&
            (now - slaves[i].last_seen) > ESPNOW_TIMEOUT_MS) {
            slaves[i].connected = false;
            memset(&slaves[i].matrix, 0, sizeof(matrix_state_t));
            DBG_PRINT("[WIFI-UDP] Slave %d timed out\n", i);
        }
    }
}
#endif // IS_MASTER

// ============================================================================
// Slave: Discovery + Update
// ============================================================================

#if IS_SLAVE
/**
 * @brief Slave: send discovery broadcast
 */
static void slave_send_discover() {
    if (discovery_complete) return;

    uint32_t now = hal::millis_now();
    if ((now - last_discover_time) < SPLIT_DISCOVER_INTERVAL_MS) return;
    last_discover_time = now;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_DISCOVER;
    pkt.slave_id = 0xFF;
    pkt.timestamp = now;
    hal::get_mac_address(pkt.mac);

    udp_broadcast_packet(&pkt);

    DBG_PRINTLN(F("[WIFI-UDP] Sent DISCOVER broadcast"));
}

/**
 * @brief Slave: process incoming UDP packets
 */
static void slave_process_rx() {
    split_packet_t pkt;
    IPAddress sender_ip;

    while (udp_receive_packet(&pkt, &sender_ip)) {
        if (pkt.type == SPLIT_PKT_DISCOVER_ACK && !discovery_complete) {
            // Master acknowledged us!
            my_slave_id = pkt.slave_id;
            master_ip = sender_ip;
            discovery_complete = true;

            DBG_PRINT("[WIFI-UDP] Paired with master! ID: %d, Master IP: %s\n",
                      my_slave_id, master_ip.toString().c_str());

        } else if (pkt.type == SPLIT_PKT_HEARTBEAT) {
            #if DEBUG_ESPNOW
            DBG_PRINTLN(F("[WIFI-UDP] Heartbeat from master"));
            #endif
        } else if (pkt.type == SPLIT_PKT_SYNC) {
            #if DEBUG_ESPNOW
            DBG_PRINTLN(F("[WIFI-UDP] Sync from master"));
            #endif
        }
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
    if (!discovery_complete) return;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_MATRIX_REPORT;
    pkt.slave_id = my_slave_id;
    pkt.timestamp = hal::millis_now();
    pkt.battery_level = 0xFF;

    for (int r = 0; r < MATRIX_ROWS; r++) {
        pkt.matrix[r] = state->rows[r];
    }

    udp_send_packet(&pkt, master_ip, SPLIT_WIFI_UDP_PORT);

    #if DEBUG_ESPNOW
    DBG_PRINTLN(F("[WIFI-UDP] Matrix report sent to master"));
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

    DBG_PRINTLN(F("[WIFI-UDP] Shutting down..."));
    udp.stop();

    #if IS_MASTER
    WiFi.softAPdisconnect(true);
    #else
    WiFi.disconnect(true);
    #endif

    DBG_PRINTLN(F("[WIFI-UDP] Shutdown complete"));
}

#endif // SPLIT_TRANSPORT_WIFI_UDP
