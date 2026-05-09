/**
 * @file split_link.cpp
 * @brief C3-Cascade — Unified Split Link Communication
 *
 * Handles both ESP-NOW and WiFi UDP transports, allowing the master to accept
 * connections from both ESP32-C3 (ESP-NOW) and Pico W (WiFi UDP) slaves.
 */

#include <Arduino.h>
#include <string.h>

#include "split_link.h"
#include "config.h"
#include "pins.h"
#include "hal/hal.h"

#if SPLIT_TRANSPORT_ESPNOW || SPLIT_TRANSPORT_WIFI_UDP

#if SPLIT_TRANSPORT_ESPNOW
#include <esp_now.h>
#include <esp_wifi.h>
#endif

#if SPLIT_TRANSPORT_WIFI_UDP
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

// ============================================================================
// Internal state
// ============================================================================

static slave_info_t slaves[MAX_SLAVE_NODES];
static uint8_t      num_slaves = 0;
static bool         discovery_complete = false;

// ----------------------------------------------------------------------------
// ESP-NOW state
// ----------------------------------------------------------------------------
#if SPLIT_TRANSPORT_ESPNOW
static volatile bool        rx_pending = false;
static split_packet_t       rx_packet;
static uint8_t              rx_sender_mac[6];
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if IS_SLAVE
static uint8_t  master_mac[6] = {0};
static uint8_t  my_slave_id_espnow = 0;
static uint32_t last_discover_time_espnow = 0;
#endif // IS_SLAVE
#endif // SPLIT_TRANSPORT_ESPNOW

// ----------------------------------------------------------------------------
// WiFi UDP state
// ----------------------------------------------------------------------------
#if SPLIT_TRANSPORT_WIFI_UDP
static WiFiUDP udp;

#if IS_MASTER
static char ap_ssid[32] = {0};
#else // IS_SLAVE
static IPAddress master_ip;
static uint8_t   my_slave_id_udp = 0;
static bool      slave_wifi_connected = false;
static uint32_t  last_discover_time_udp = 0;
static uint32_t  last_wifi_retry_time = 0;
#endif // IS_MASTER / IS_SLAVE
#endif // SPLIT_TRANSPORT_WIFI_UDP

#if IS_MASTER
static uint32_t last_heartbeat_time = 0;
#endif

// ============================================================================
// Helpers
// ============================================================================

static int find_slave_by_mac(const uint8_t* mac) {
    for (uint8_t i = 0; i < num_slaves; i++) {
        if (memcmp(slaves[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

#if SPLIT_TRANSPORT_ESPNOW
static bool add_peer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
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

static void on_data_recv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (data_len != sizeof(split_packet_t)) return;
    memcpy((void*)&rx_packet, data, sizeof(split_packet_t));
    memcpy(rx_sender_mac, mac_addr, 6);
    rx_pending = true;
}

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {}
#endif

#if SPLIT_TRANSPORT_WIFI_UDP
#if IS_MASTER
static void build_ap_ssid() {
    uint8_t mac[6];
    hal::get_mac_address(mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X", SPLIT_WIFI_AP_PREFIX, mac[4], mac[5]);
}
#endif

static void udp_send_packet(const split_packet_t* pkt, uint32_t ip, uint16_t port) {
    udp.beginPacket(IPAddress(ip), port);
    udp.write((const uint8_t*)pkt, sizeof(split_packet_t));
    udp.endPacket();
}

#if IS_SLAVE
static void udp_broadcast_packet(const split_packet_t* pkt) {
    udp.beginPacket(IPAddress(255, 255, 255, 255), SPLIT_WIFI_UDP_PORT);
    udp.write((const uint8_t*)pkt, sizeof(split_packet_t));
    udp.endPacket();
}
#endif

static bool udp_receive_packet(split_packet_t* pkt, uint32_t* sender_ip) {
    int packetSize = udp.parsePacket();
    if (packetSize != sizeof(split_packet_t)) return false;
    *sender_ip = (uint32_t)udp.remoteIP();
    udp.read((uint8_t*)pkt, sizeof(split_packet_t));
    return true;
}

#if IS_SLAVE
static bool slave_scan_and_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    DBG_PRINTLN(F("[WIFI-UDP] Slave: scanning for master AP..."));

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.startsWith(SPLIT_WIFI_AP_PREFIX)) {
            DBG_PRINT("[WIFI-UDP] Found master AP: %s\n", ssid.c_str());
            WiFi.begin(ssid.c_str(), SPLIT_WIFI_PASSWORD);
            uint32_t connect_start = hal::millis_now();
            while (WiFi.status() != WL_CONNECTED && (hal::millis_now() - connect_start) < 8000) {
                delay(100);
            }
            if (WiFi.status() == WL_CONNECTED) {
                DBG_PRINT("\n[WIFI-UDP] Connected! IP=%s\n", WiFi.localIP().toString().c_str());
                WiFi.scanDelete();
                return true;
            } else {
                WiFi.disconnect();
            }
        }
    }
    WiFi.scanDelete();
    return false;
}
#endif // IS_SLAVE
#endif // SPLIT_TRANSPORT_WIFI_UDP


// ============================================================================
// Initialization
// ============================================================================

void split_link_init() {
    DBG_PRINTLN(F("[SPLIT] Initializing split link..."));

    // Set up WiFi modes properly for all combinations
    #if IS_MASTER
        #if SPLIT_TRANSPORT_ESPNOW && SPLIT_TRANSPORT_WIFI_UDP
            WiFi.mode(WIFI_AP_STA);
            WiFi.disconnect();
            DBG_PRINTLN(F("[SPLIT] WiFi Mode: AP+STA (Dual Mode)"));
        #elif SPLIT_TRANSPORT_ESPNOW
            WiFi.mode(WIFI_STA);
            WiFi.disconnect();
            DBG_PRINTLN(F("[SPLIT] WiFi Mode: STA (ESP-NOW only)"));
        #elif SPLIT_TRANSPORT_WIFI_UDP
            WiFi.mode(WIFI_AP);
            DBG_PRINTLN(F("[SPLIT] WiFi Mode: AP (UDP only)"));
        #endif
    #else
        // Slave initialization sets its own WiFi mode
    #endif

    #if SPLIT_TRANSPORT_WIFI_UDP
        #if IS_MASTER
            build_ap_ssid();
            WiFi.softAP(ap_ssid, SPLIT_WIFI_PASSWORD);
            delay(100);
            DBG_PRINT("[WIFI-UDP] SoftAP started: %s IP=%s\n", ap_ssid, WiFi.softAPIP().toString().c_str());
            udp.begin(SPLIT_WIFI_UDP_PORT);
        #else
            if (slave_scan_and_connect()) {
                slave_wifi_connected = true;
                udp.begin(SPLIT_WIFI_UDP_PORT);
                DBG_PRINTLN(F("[WIFI-UDP] Connected to master AP"));
            } else {
                slave_wifi_connected = false;
                last_wifi_retry_time = hal::millis_now();
            }
        #endif
    #endif

    #if SPLIT_TRANSPORT_ESPNOW
        if (esp_now_init() != ESP_OK) {
            DBG_PRINTLN(F("[ESPNOW] ERROR: init failed!"));
        } else {
            esp_now_register_recv_cb(on_data_recv);
            esp_now_register_send_cb(on_data_sent);
            add_peer(BROADCAST_MAC);
            DBG_PRINTLN(F("[ESPNOW] Init success"));
        }
    #endif

    #if IS_MASTER
        num_slaves = 0;
        for (uint8_t i = 0; i < MAX_SLAVE_NODES; i++) {
            memset(&slaves[i], 0, sizeof(slave_info_t));
            slaves[i].transport = TRANSPORT_NONE;
        }
    #endif

    discovery_complete = false;
}

// ============================================================================
// Master: Discovery & Update
// ============================================================================
#if IS_MASTER

static void master_register_slave(const uint8_t* mac, split_transport_t transport, uint32_t ip_addr, int* idx_out) {
    int idx = find_slave_by_mac(mac);
    if (idx < 0) {
        if (num_slaves >= MAX_SLAVE_NODES) return;
        idx = num_slaves++;
        memcpy(slaves[idx].mac, mac, 6);
        memset(&slaves[idx].matrix, 0, sizeof(matrix_state_t));
        slaves[idx].discovered = true;
        
        #if SPLIT_TRANSPORT_ESPNOW
        if (transport == TRANSPORT_ESPNOW) {
            add_peer(mac);
        }
        #endif
    }
    
    slaves[idx].transport = transport;
    slaves[idx].ip_address = ip_addr;
    slaves[idx].last_seen = hal::millis_now();
    slaves[idx].battery_level = 0xFF;
    slaves[idx].connected = true;
    *idx_out = idx;
}

static void master_process_espnow_rx() {
#if SPLIT_TRANSPORT_ESPNOW
    if (!rx_pending) return;
    rx_pending = false;

    if (rx_packet.type == SPLIT_PKT_DISCOVER) {
        int idx = -1;
        master_register_slave(rx_sender_mac, TRANSPORT_ESPNOW, 0, &idx);
        if (idx >= 0) {
            split_packet_t ack = {};
            ack.type = SPLIT_PKT_DISCOVER_ACK;
            ack.slave_id = (uint8_t)idx;
            ack.timestamp = hal::millis_now();
            hal::get_mac_address(ack.mac);
            esp_now_send(rx_sender_mac, (uint8_t*)&ack, sizeof(ack));
            discovery_complete = true;
            DBG_PRINT("[ESPNOW] Registered slave %d\n", idx);
        }
    } else if (rx_packet.type == SPLIT_PKT_MATRIX_REPORT) {
        uint8_t sid = rx_packet.slave_id;
        if (sid < num_slaves && slaves[sid].transport == TRANSPORT_ESPNOW) {
            for (int r = 0; r < MATRIX_ROWS; r++) slaves[sid].matrix.rows[r] = rx_packet.matrix[r];
            slaves[sid].last_seen = hal::millis_now();
            slaves[sid].battery_level = rx_packet.battery_level;
            slaves[sid].connected = true;
        }
    }
#endif
}

static void master_process_udp_rx() {
#if SPLIT_TRANSPORT_WIFI_UDP
    split_packet_t pkt;
    uint32_t sender_ip;

    while (udp_receive_packet(&pkt, &sender_ip)) {
        if (pkt.type == SPLIT_PKT_DISCOVER) {
            int idx = -1;
            master_register_slave(pkt.mac, TRANSPORT_WIFI_UDP, sender_ip, &idx);
            if (idx >= 0) {
                split_packet_t ack = {};
                ack.type = SPLIT_PKT_DISCOVER_ACK;
                ack.slave_id = (uint8_t)idx;
                ack.timestamp = hal::millis_now();
                hal::get_mac_address(ack.mac);
                udp_send_packet(&ack, sender_ip, SPLIT_WIFI_UDP_PORT);
                discovery_complete = true;
                DBG_PRINT("[WIFI-UDP] Registered slave %d\n", idx);
            }
        } else if (pkt.type == SPLIT_PKT_MATRIX_REPORT) {
            uint8_t sid = pkt.slave_id;
            if (sid < num_slaves && slaves[sid].transport == TRANSPORT_WIFI_UDP) {
                for (int r = 0; r < MATRIX_ROWS; r++) slaves[sid].matrix.rows[r] = pkt.matrix[r];
                slaves[sid].last_seen = hal::millis_now();
                slaves[sid].battery_level = pkt.battery_level;
                slaves[sid].connected = true;
                slaves[sid].ip_address = sender_ip;
            }
        }
    }
#endif
}

static void master_send_heartbeat() {
    uint32_t now = hal::millis_now();
    if ((now - last_heartbeat_time) < ESPNOW_HEARTBEAT_MS) return;
    last_heartbeat_time = now;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_HEARTBEAT;
    pkt.slave_id = 0xFF;
    pkt.timestamp = now;

    for (uint8_t i = 0; i < num_slaves; i++) {
        if (!slaves[i].connected) continue;

        if ((now - slaves[i].last_seen) > ESPNOW_TIMEOUT_MS) {
            slaves[i].connected = false;
            memset(&slaves[i].matrix, 0, sizeof(matrix_state_t));
            DBG_PRINT("[SPLIT] Slave %d timed out\n", i);
            continue;
        }

        #if SPLIT_TRANSPORT_ESPNOW
        if (slaves[i].transport == TRANSPORT_ESPNOW) {
            esp_now_send(slaves[i].mac, (uint8_t*)&pkt, sizeof(pkt));
        }
        #endif
        
        #if SPLIT_TRANSPORT_WIFI_UDP
        if (slaves[i].transport == TRANSPORT_WIFI_UDP) {
            udp_send_packet(&pkt, slaves[i].ip_address, SPLIT_WIFI_UDP_PORT);
        }
        #endif
    }
}
#endif // IS_MASTER

// ============================================================================
// Slave: Discovery & Update
// ============================================================================
#if IS_SLAVE

static void slave_update_espnow() {
#if SPLIT_TRANSPORT_ESPNOW
    if (!discovery_complete) {
        uint32_t now = hal::millis_now();
        if ((now - last_discover_time_espnow) >= SPLIT_DISCOVER_INTERVAL_MS) {
            last_discover_time_espnow = now;
            split_packet_t pkt = {};
            pkt.type = SPLIT_PKT_DISCOVER;
            pkt.slave_id = 0xFF;
            pkt.timestamp = now;
            hal::get_mac_address(pkt.mac);
            esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
        }
    }

    if (rx_pending) {
        rx_pending = false;
        if (rx_packet.type == SPLIT_PKT_DISCOVER_ACK) {
            my_slave_id_espnow = rx_packet.slave_id;
            memcpy(master_mac, rx_packet.mac, 6);
            discovery_complete = true;
            add_peer(master_mac);
            DBG_PRINT("[ESPNOW] Paired! ID: %d\n", my_slave_id_espnow);
        }
    }
#endif
}

static void slave_update_udp() {
#if SPLIT_TRANSPORT_WIFI_UDP
    if (slave_wifi_connected && WiFi.status() != WL_CONNECTED) {
        slave_wifi_connected = false;
        discovery_complete = false;
        my_slave_id_udp = 0;
    }

    if (!slave_wifi_connected && !discovery_complete) {
        uint32_t now = hal::millis_now();
        if ((now - last_wifi_retry_time) >= SPLIT_WIFI_RETRY_INTERVAL_MS) {
            last_wifi_retry_time = now;
            if (slave_scan_and_connect()) {
                slave_wifi_connected = true;
                udp.stop();
                udp.begin(SPLIT_WIFI_UDP_PORT);
            }
        }
    }

    if (slave_wifi_connected) {
        if (!discovery_complete) {
            uint32_t now = hal::millis_now();
            if ((now - last_discover_time_udp) >= SPLIT_DISCOVER_INTERVAL_MS) {
                last_discover_time_udp = now;
                split_packet_t pkt = {};
                pkt.type = SPLIT_PKT_DISCOVER;
                pkt.slave_id = 0xFF;
                pkt.timestamp = now;
                hal::get_mac_address(pkt.mac);
                udp_broadcast_packet(&pkt);
            }
        }

        split_packet_t pkt;
        uint32_t sender_ip;
        while (udp_receive_packet(&pkt, &sender_ip)) {
            if (pkt.type == SPLIT_PKT_DISCOVER_ACK && !discovery_complete) {
                my_slave_id_udp = pkt.slave_id;
                master_ip = IPAddress(sender_ip);
                discovery_complete = true;
                DBG_PRINT("[WIFI-UDP] Paired! ID: %d\n", my_slave_id_udp);
            }
        }
    }
#endif
}
#endif // IS_SLAVE

// ============================================================================
// Public API
// ============================================================================

void split_link_update() {
    #if !ENABLE_SPLIT_LINK
    return;
    #endif

    #if IS_MASTER
        master_process_espnow_rx();
        master_process_udp_rx();
        if (num_slaves > 0) master_send_heartbeat();
    #else
        slave_update_espnow();
        slave_update_udp();
    #endif
}

void split_link_send_matrix(const matrix_state_t* state) {
    #if !ENABLE_SPLIT_LINK || !IS_SLAVE
    (void)state;
    return;
    #endif

    #if IS_SLAVE
    if (!discovery_complete) return;

    split_packet_t pkt = {};
    pkt.type = SPLIT_PKT_MATRIX_REPORT;
    pkt.timestamp = hal::millis_now();
    pkt.battery_level = 0xFF;
    for (int r = 0; r < MATRIX_ROWS; r++) pkt.matrix[r] = state->rows[r];

    #if SPLIT_TRANSPORT_ESPNOW
        pkt.slave_id = my_slave_id_espnow;
        esp_now_send(master_mac, (uint8_t*)&pkt, sizeof(pkt));
    #endif

    #if SPLIT_TRANSPORT_WIFI_UDP
    if (slave_wifi_connected) {
        pkt.slave_id = my_slave_id_udp;
        udp_send_packet(&pkt, (uint32_t)master_ip, SPLIT_WIFI_UDP_PORT);
    }
    #endif
    #endif // IS_SLAVE
}

const matrix_state_t* split_link_get_slave_matrix(uint8_t slave_id) {
    #if IS_MASTER
    if (slave_id < num_slaves && slaves[slave_id].connected) {
        return &slaves[slave_id].matrix;
    }
    #endif
    return nullptr;
}

bool split_link_slave_connected(uint8_t slave_id) {
    #if IS_MASTER
    if (slave_id < num_slaves) return slaves[slave_id].connected;
    #endif
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

    #if SPLIT_TRANSPORT_ESPNOW
    esp_now_deinit();
    #endif

    #if SPLIT_TRANSPORT_WIFI_UDP
    udp.stop();
    #if IS_MASTER
    WiFi.softAPdisconnect(true);
    #else
    WiFi.disconnect(true);
    #endif
    #endif
}

void split_link_reset_pairing() {
    #if IS_MASTER
    num_slaves = 0;
    for (uint8_t i = 0; i < MAX_SLAVE_NODES; i++) {
        memset(&slaves[i], 0, sizeof(slave_info_t));
        slaves[i].transport = TRANSPORT_NONE;
    }
    DBG_PRINTLN(F("[PAIR] Reset — accepting new slaves"));
    #else
    #if SPLIT_TRANSPORT_ESPNOW
    my_slave_id_espnow = 0;
    memset(master_mac, 0, 6);
    #endif
    #if SPLIT_TRANSPORT_WIFI_UDP
    my_slave_id_udp = 0;
    slave_wifi_connected = false;
    last_wifi_retry_time = 0;
    #endif
    discovery_complete = false;
    #endif
}

#else // No transport available

void split_link_init() {}
void split_link_update() {}
void split_link_send_matrix(const matrix_state_t* state) { (void)state; }
const matrix_state_t* split_link_get_slave_matrix(uint8_t slave_id) { return nullptr; }
bool split_link_slave_connected(uint8_t slave_id) { return false; }
uint8_t split_link_slave_count() { return 0; }
bool split_link_is_paired() { return false; }
void split_link_shutdown() {}
void split_link_reset_pairing() {}

#endif // Transport selection
