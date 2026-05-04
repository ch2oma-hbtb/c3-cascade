/**
 * @file main.cpp
 * @brief C3-Cascade — Firmware entry point
 *
 * Main loop orchestrates:
 *   1. Key matrix scanning (local half)
 *   2. Receiving slave matrix data (via ESP-NOW)
 *   3. Merging matrices and resolving keycodes
 *   4. Sending HID keyboard reports (via BLE)
 *   5. Idle detection and deep sleep entry
 *
 * Roles:
 *   MASTER — Scans local matrix, receives slave data, sends BLE HID reports
 *   SLAVE  — Scans local matrix, sends matrix data to master via ESP-NOW
 */

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "pins.h"
#include "keymap.h"
#include "hal/hal.h"
#include "matrix.h"
#include "ble_hid.h"
#include "split_link.h"
#include "power.h"

#if defined(BOARD_XIAO_ESP32C3) || defined(BOARD_XIAO_ESP32C6)
#include "hal/hal_esp32.h"
#endif

// ============================================================================
// State
// ============================================================================

static uint8_t current_layer = 0;
static uint32_t last_hid_report_time = 0;

// ============================================================================
// Keycode resolution
// ============================================================================

/**
 * @brief Build an HID report from the merged matrix state
 *
 * Walks through the matrix, looks up keycodes in the current layer,
 * separates modifiers from regular keys, and fills the HID report.
 *
 * @param local_matrix   Matrix state from the local half
 * @param slave_matrix   Matrix state from a slave half (can be nullptr)
 * @param report         Output HID report
 */
static void build_hid_report(const matrix_state_t* local_matrix,
                             const matrix_state_t* slave_matrix,
                             hid_keyboard_report_t* report) {
    memset(report, 0, sizeof(hid_keyboard_report_t));
    uint8_t key_count = 0;

    // Process local matrix
    if (local_matrix != nullptr) {
        for (int r = 0; r < MATRIX_ROWS; r++) {
            for (int c = 0; c < MATRIX_COLS; c++) {
                if (local_matrix->rows[r] & (1 << c)) {
                    uint16_t keycode = KEYMAP_LAYERS_PRIMARY[current_layer][r][c];

                    if (keycode == HID_KEY_NONE) continue;

                    if (IS_MOD_KEY(keycode)) {
                        // Modifier key — set bit in modifier byte
                        report->modifiers |= GET_MOD_BIT(keycode);
                    } else {
                        // Regular key — add to key array (up to 6KRO)
                        if (key_count < HID_MAX_KEYS) {
                            report->keys[key_count++] = (uint8_t)keycode;
                        }
                    }
                }
            }
        }
    }

    // Process slave matrix (if connected)
    // Note: The slave's keymap would be different (right half layout).
    // For now, we use the same keymap — update when secondary layout is added.
    if (slave_matrix != nullptr) {
        for (int r = 0; r < MATRIX_ROWS; r++) {
            for (int c = 0; c < MATRIX_COLS; c++) {
                if (slave_matrix->rows[r] & (1 << c)) {
                    // Use the secondary (right half) keymap for slave data
                    uint16_t keycode = KEYMAP_LAYERS_SECONDARY[current_layer][r][c];

                    if (keycode == HID_KEY_NONE) continue;

                    if (IS_MOD_KEY(keycode)) {
                        report->modifiers |= GET_MOD_BIT(keycode);
                    } else {
                        if (key_count < HID_MAX_KEYS) {
                            report->keys[key_count++] = (uint8_t)keycode;
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Helper: check if slave data needs to trigger a report update
// ============================================================================

/**
 * @brief Checks if any slave data was recently received that needs
 *        to be reflected in the HID report.
 */
static bool rx_needs_update() {
    #if IS_MASTER && ENABLE_SPLIT_LINK
    for (uint8_t i = 0; i < split_link_slave_count(); i++) {
        if (split_link_slave_connected(i)) {
            return true;
        }
    }
    #endif
    return false;
}

// ============================================================================
// setup()
// ============================================================================

void setup() {
    // 1. Initialize HAL (GPIO, serial, etc.)
    hal::init();

    // 2. Initialize power management (checks wake reason)
    power_init();

    // 3. Initialize key matrix scanner
    matrix_init();

    #if IS_MASTER
    // ---- Master-specific initialization ----

    // 4. Initialize Wi-Fi (STA mode for ESP-NOW)
    #if HAS_ESPNOW
    hal::esp32::wifi_init_sta();
    #endif

    // 5. Initialize ESP-NOW split link
    #if ENABLE_SPLIT_LINK
    split_link_init();
    #endif

    // 6. Initialize BLE HID keyboard
    #if ENABLE_BLE_HID
    ble_hid_init();
    #endif

    DBG_PRINTLN(F("\n[MAIN] Master node ready!"));
    DBG_PRINTLN(F("[MAIN] Scanning matrix & waiting for BLE connection..."));

    #else
    // ---- Slave-specific initialization ----

    // 4. Initialize Wi-Fi (STA mode for ESP-NOW)
    #if HAS_ESPNOW
    hal::esp32::wifi_init_sta();
    #endif

    // 5. Initialize ESP-NOW split link
    #if ENABLE_SPLIT_LINK
    split_link_init();
    #endif

    DBG_PRINTLN(F("\n[MAIN] Slave node ready!"));
    DBG_PRINTLN(F("[MAIN] Scanning matrix & sending to master..."));

    #endif // IS_MASTER / IS_SLAVE

    DBG_PRINT("[MAIN] Deep sleep timeout: %d ms\n", DEEPSLEEP_TIMEOUT_MS);
    DBG_PRINTLN(F("========================================\n"));
}

// ============================================================================
// loop()
// ============================================================================

void loop() {
    uint32_t now = hal::millis_now();

    // --- Step 1: Scan local key matrix ---
    bool local_changed = matrix_scan();

    #if IS_MASTER
    // --- Step 2: Receive and process slave data ---
    #if ENABLE_SPLIT_LINK
    split_link_update();
    #endif

    // --- Step 3: Build and send HID report if something changed ---
    #if ENABLE_BLE_HID
    if (local_changed || rx_needs_update()) {
        // Rate-limit HID reports
        if ((now - last_hid_report_time) >= HID_REPORT_INTERVAL_MS) {
            const matrix_state_t* local = matrix_get_state();

            // Get slave matrix (slave 0, if connected)
            const matrix_state_t* slave = nullptr;
            #if ENABLE_SPLIT_LINK
            slave = split_link_get_slave_matrix(0);
            #endif

            // Build combined HID report
            hid_keyboard_report_t report;
            build_hid_report(local, slave, &report);

            // Send via BLE
            ble_hid_send_report(&report);
            last_hid_report_time = now;
        }
    }
    #endif // ENABLE_BLE_HID

    #else // IS_SLAVE
    // --- Slave: Send matrix to master if changed ---
    if (local_changed) {
        const matrix_state_t* state = matrix_get_state();
        split_link_send_matrix(state);
    }

    // Process any incoming master data (heartbeats, sync)
    #if ENABLE_SPLIT_LINK
    split_link_update();
    #endif

    #endif // IS_MASTER / IS_SLAVE

    // --- Step 4: Check for idle timeout → deep sleep ---
    #if ENABLE_DEEPSLEEP
    power_check_sleep(matrix_last_activity());
    #endif

    // --- Step 5: Yield / small delay ---
    // delayMicroseconds is handled by the scan interval
    hal::delay_us(MATRIX_SCAN_INTERVAL_US);
}


