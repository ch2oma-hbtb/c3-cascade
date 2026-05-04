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
#else
    #error "No board type defined! Add -DBOARD_xxx to build_flags."
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
#define ENABLE_SPLIT_LINK       1       // ESP-NOW inter-half communication
#define ENABLE_DEEPSLEEP        1       // Deep sleep power management
#define ENABLE_BATTERY_REPORT   0       // BLE battery level reporting (future)
#define ENABLE_RGB              0       // RGB LED support (future)

// ============================================================================
// Matrix dimensions
// ============================================================================
#define MATRIX_ROWS             5       // ROW0–ROW4
#define MATRIX_COLS             7       // COL0–COL6
#define MAX_KEYS                (MATRIX_ROWS * MATRIX_COLS)

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

// BLE
#define BLE_DEVICE_NAME         "C3-Cascade"
#define BLE_MANUFACTURER        "C3-Cascade"
#define BLE_RECONNECT_TIMEOUT   5000    // ms to wait for reconnection after wake

// ============================================================================
// HID report settings
// ============================================================================
#define HID_MAX_KEYS            6       // Standard 6-Key Rollover (6KRO)

// ============================================================================
// Split keyboard settings
// ============================================================================
#define MAX_SLAVE_NODES         2       // Maximum number of slave halves

// Master MAC address — update this with your actual master board MAC
// Used by slave nodes to send data to master
#define MASTER_MAC              {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Slave MAC addresses — update with actual slave board MACs
// Used by master to register peers
#define SLAVE_MAC_1             {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define SLAVE_MAC_2             {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

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
