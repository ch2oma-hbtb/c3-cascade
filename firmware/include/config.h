/**
 * @file config.h
 * @brief C3-Cascade — Central firmware configuration
 *
 * Board type is set via PlatformIO build flags (-DBOARD_XIAO_ESP32C3, etc.)
 * Split role is set via build flags (-DSPLIT_ROLE_MASTER or -DSPLIT_ROLE_SLAVE)
 */

#ifndef C3CASCADE_CONFIG_H
#define C3CASCADE_CONFIG_H

#include <stdint.h>

// ============================================================================
// Board type detection (set by platformio.ini build_flags)
// ============================================================================
// Undefine BOARD_NAME if set by the platform (we use our own names)
#ifdef BOARD_NAME
    #undef BOARD_NAME
#endif

#if defined(BOARD_XIAO_ESP32C3)
    #define BOARD_NAME          "XIAO ESP32-C3"
    #define HAS_ESPNOW          1
    #define HAS_NIMBLE           1
    #define HAS_WIFI             1
#elif defined(BOARD_XIAO_ESP32C6)
    #define BOARD_NAME          "XIAO ESP32-C6"
    #define HAS_ESPNOW          1
    #define HAS_NIMBLE           1
    #define HAS_WIFI             1
#elif defined(BOARD_NRF52840)
    #define BOARD_NAME          "nRF52840"
    #define HAS_ESPNOW          0
    #define HAS_NIMBLE           0
    #define HAS_WIFI             0
    #define HAS_ADAFRUIT_BLE     1
    #define HAS_BTSTACK          0
#elif defined(BOARD_PICO2_W)
    #define BOARD_NAME          "Pico 2W (RP2350)"
    #define HAS_ESPNOW          0
    #define HAS_NIMBLE           0
    #define HAS_WIFI             1
    #define HAS_ADAFRUIT_BLE     0
    #define HAS_BTSTACK          1
#elif defined(BOARD_PICO_W)
    #define BOARD_NAME          "Pico W (RP2040)"
    #define HAS_ESPNOW          0
    #define HAS_NIMBLE           0
    #define HAS_WIFI             1
    #define HAS_ADAFRUIT_BLE     0
    #define HAS_BTSTACK          0
#else
    #error "No board type defined! Add -DBOARD_xxx to build_flags."
#endif

// ============================================================================
// Split link transport selection (auto-detected, overridable)
// ============================================================================
#if defined(FORCE_WIFI_UDP)
    #define SPLIT_TRANSPORT_ESPNOW   0
    #define SPLIT_TRANSPORT_WIFI_UDP 1
#elif defined(SPLIT_ROLE_MASTER) && HAS_ESPNOW && HAS_WIFI
    #define SPLIT_TRANSPORT_ESPNOW   1
    #define SPLIT_TRANSPORT_WIFI_UDP 1
#elif HAS_ESPNOW
    #define SPLIT_TRANSPORT_ESPNOW   1
    #define SPLIT_TRANSPORT_WIFI_UDP 0
#elif HAS_WIFI
    #define SPLIT_TRANSPORT_ESPNOW   0
    #define SPLIT_TRANSPORT_WIFI_UDP 1
#else
    #define SPLIT_TRANSPORT_ESPNOW   0
    #define SPLIT_TRANSPORT_WIFI_UDP 0
#endif

// ============================================================================
// Split keyboard role (set by platformio.ini build_flags)
// ============================================================================
#if !defined(SPLIT_ROLE_MASTER) && !defined(SPLIT_ROLE_SLAVE)
    #define SPLIT_ROLE_MASTER   // default to master
#endif

#ifdef SPLIT_ROLE_MASTER
    #define IS_MASTER           1
    #define IS_SLAVE            0
#else
    #define IS_MASTER           0
    #define IS_SLAVE            1
#endif

// ============================================================================
// Feature toggles
// ============================================================================
#define ENABLE_BLE_HID          1       // BLE HID keyboard output (master only)
#define ENABLE_SPLIT_LINK       1       // Wireless inter-half communication (ESP-NOW or WiFi UDP)
#define ENABLE_DEEPSLEEP        1       // Deep sleep power management
#define ENABLE_BATTERY_REPORT   0       // BLE battery level reporting (future)
#define ENABLE_RGB              0       // RGB LED support (future)
#define ENABLE_LED              1       // Onboard LED for status indication

// ============================================================================
// Matrix dimensions
// ============================================================================
#define MATRIX_ROWS             5       // ROW0–ROW4
#define MATRIX_COLS             8       // COL0–COL7 (max across all halves)
#define MAX_KEYS                (MATRIX_ROWS * MATRIX_COLS)

// ============================================================================
// Per-board physical column count and virtual column configuration
// ============================================================================
#if defined(BOARD_XIAO_ESP32C3) || defined(BOARD_XIAO_ESP32C6)
    #define NUM_PHYSICAL_COLS   6       // 6 real GPIO column pins
    #define HAS_VIRTUAL_COL     1       // COL6 is row-to-row wired
    #define VIRTUAL_COL_INDEX   6       // Bit position of the virtual column
#elif defined(BOARD_PICO2_W)
    #define NUM_PHYSICAL_COLS   7       // 7 real GPIO column pins
    #define HAS_VIRTUAL_COL     0
#elif defined(BOARD_PICO_W)
    #define NUM_PHYSICAL_COLS   8       // 8 real GPIO column pins
    #define HAS_VIRTUAL_COL     0
#elif defined(BOARD_NRF52840)
    #define NUM_PHYSICAL_COLS   6
    #define HAS_VIRTUAL_COL     1
    #define VIRTUAL_COL_INDEX   6
#else
    #error "Define NUM_PHYSICAL_COLS and HAS_VIRTUAL_COL for this board"
#endif

// ============================================================================
// Timing constants
// ============================================================================
#define MATRIX_SCAN_INTERVAL_US 1000    // 1ms between full matrix scans (1kHz)
#define DEBOUNCE_MS             5       // Key debounce time
#define HID_REPORT_INTERVAL_MS  8       // Min time between HID reports (~125Hz)

// Deep sleep
#define DEEPSLEEP_TIMEOUT_MS    60000   // 60 seconds of inactivity → deep sleep

// ESP-NOW
#define ESPNOW_CHANNEL          1       // Wi-Fi channel for ESP-NOW
#define ESPNOW_HEARTBEAT_MS     1000    // Heartbeat interval (master → slave)
#define ESPNOW_TIMEOUT_MS       5000    // Slave considered disconnected after this

// WiFi UDP split link
#define SPLIT_WIFI_AP_PREFIX    "C3C-"          // SoftAP SSID prefix (master appends MAC)
#define SPLIT_WIFI_PASSWORD     "c3cascade"     // SoftAP password (WPA2)
#define SPLIT_WIFI_UDP_PORT     4200            // UDP port for split communication

// Auto-discovery
#define SPLIT_DISCOVER_INTERVAL_MS  500     // Slave broadcasts discover every 500ms
#define SPLIT_DISCOVER_TIMEOUT_MS   15000   // Per-attempt WiFi scan timeout
#define SPLIT_WIFI_RETRY_INTERVAL_MS 5000   // Time between WiFi AP scan retries

// BLE
#define BLE_DEVICE_NAME         "C3-Cascade"
#define BLE_MANUFACTURER        "C3-Cascade"
#define BLE_RECONNECT_TIMEOUT   5000    // ms to wait for reconnection after wake

// Pairing shortcuts (master only)
#define PAIRING_SHORTCUT_HOLD_MS    5000    // Hold time for pairing shortcuts
#define PAIRING_MODE_TIMEOUT_MS    60000   // Auto-exit pairing mode after 60s

// LED blink
#define LED_BLINK_INTERVAL_MS      300     // LED toggle interval during pairing

// ============================================================================
// HID report settings
// ============================================================================
#define HID_MAX_KEYS            6       // Standard 6-Key Rollover (6KRO)

// ============================================================================
// Split keyboard settings
// ============================================================================
#define MAX_SLAVE_NODES         2       // Maximum number of slave halves

// NOTE: MAC addresses are no longer needed — auto-discovery handles pairing.
// The old MASTER_MAC / SLAVE_MAC_x constants have been removed.
// See split_link.cpp and split_link_wifi_udp.cpp for the discovery protocol.

// ============================================================================
// Debug
// ============================================================================
#define DEBUG_SERIAL            1       // Enable serial debug output
#define DEBUG_MATRIX            0       // Print matrix state on each scan
#define DEBUG_BLE               0       // Print BLE events
#define DEBUG_ESPNOW            0       // Print ESP-NOW events

#if DEBUG_SERIAL
    #define DBG_PRINT(...)      Serial.printf(__VA_ARGS__)
    #define DBG_PRINTLN(x)      Serial.println(x)
#else
    #define DBG_PRINT(...)
    #define DBG_PRINTLN(x)
#endif

#endif // C3CASCADE_CONFIG_H
