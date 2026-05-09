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
#elif defined(BOARD_PICO2_W) || defined(BOARD_PICO_W)
#include "hal/hal_rp2040.h"
#endif

// ============================================================================
// State
// ============================================================================

static uint8_t current_layer = 0;
static uint32_t last_hid_report_time = 0;

// ============================================================================
// Pairing mode state (master only)
// ============================================================================

#if IS_MASTER
enum PairingMode { PAIRING_NONE, PAIRING_PC_BLE, PAIRING_SLAVE_SEARCH };
static PairingMode pairing_mode = PAIRING_NONE;
static uint32_t pairing_start_time = 0;
#endif

#if ENABLE_LED && LED_PIN != 0xFF
// LED blink state
static bool led_blink_state = false;
static uint32_t last_led_toggle_time = 0;
#endif

// Pairing key positions on master (primary keymap)
#define PAIR_ROW_ESC     0
#define PAIR_COL_ESC     0
#define PAIR_ROW_SHIFT   3
#define PAIR_COL_SHIFT   0
#define PAIR_ROW_SPACE   4
#define PAIR_COL_SPACE   4

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
                    // During pairing mode, only allow pairing-related keys
                    #if IS_MASTER
                    if (pairing_mode != PAIRING_NONE) {
                        bool is_pair_key = (r == PAIR_ROW_ESC && c == PAIR_COL_ESC) ||
                                            (r == PAIR_ROW_SHIFT && c == PAIR_COL_SHIFT) ||
                                            (r == PAIR_ROW_SPACE && c == PAIR_COL_SPACE);
                        if (!is_pair_key) continue;
                    }
                    #endif

                    uint16_t keycode = KEYMAP_LAYERS_PRIMARY[current_layer][r][c];

                    if (keycode == HID_KEY_NONE || keycode == HID_KEY_FN) continue;

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
    // Suppress all slave keys during pairing mode
    if (slave_matrix != nullptr) {
        #if IS_MASTER
        if (pairing_mode == PAIRING_NONE)
        #endif
        {
            for (int r = 0; r < MATRIX_ROWS; r++) {
                for (int c = 0; c < MATRIX_COLS; c++) {
                    if (slave_matrix->rows[r] & (1 << c)) {
                        // Use the secondary (right half) keymap for slave data
                        uint16_t keycode = KEYMAP_LAYERS_SECONDARY[current_layer][r][c];

                        if (keycode == HID_KEY_NONE || keycode == HID_KEY_FN) continue;

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
// Master: Pairing shortcut detection
// ============================================================================

#if IS_MASTER

static uint32_t pc_pairing_hold_start = 0;
static uint32_t slave_pairing_hold_start = 0;
static bool pc_pairing_triggered = false;
static bool slave_pairing_triggered = false;

/**
 * @brief Check for pairing shortcuts held on the local matrix
 *
 * ESC + SPACE  (5s) → Enter BLE PC pairing mode
 * ESC + SHIFT  (5s) → Reset slave pairing / accept new slaves
 */
static void check_pairing_shortcuts(const matrix_state_t* state) {
    bool esc_pressed = (state->rows[0] & (1 << 0)) != 0;
    bool space_pressed = (state->rows[4] & (1 << 4)) != 0;
    bool shift_pressed = (state->rows[3] & (1 << 0)) != 0;

    uint32_t now = hal::millis_now();

    // PC pairing: ESC + SPACE held for 5s
    if (esc_pressed && space_pressed) {
        if (pc_pairing_hold_start == 0) {
            pc_pairing_hold_start = now;
        } else if (!pc_pairing_triggered &&
                   (now - pc_pairing_hold_start) >= PAIRING_SHORTCUT_HOLD_MS) {
            pc_pairing_triggered = true;
            pairing_mode = PAIRING_PC_BLE;
            pairing_start_time = now;
            DBG_PRINTLN(F("[PAIR] Shortcut triggered: PC BLE pairing mode"));
            #if ENABLE_BLE_HID
            ble_hid_enter_pairing_mode();
            #endif
        }
    } else {
        pc_pairing_hold_start = 0;
        pc_pairing_triggered = false;
    }

    // Slave pairing: ESC + SHIFT held for 5s
    if (esc_pressed && shift_pressed) {
        if (slave_pairing_hold_start == 0) {
            slave_pairing_hold_start = now;
        } else if (!slave_pairing_triggered &&
                   (now - slave_pairing_hold_start) >= PAIRING_SHORTCUT_HOLD_MS) {
            slave_pairing_triggered = true;
            pairing_mode = PAIRING_SLAVE_SEARCH;
            pairing_start_time = now;
            DBG_PRINTLN(F("[PAIR] Shortcut triggered: Slave pairing reset"));
            #if ENABLE_SPLIT_LINK
            split_link_reset_pairing();
            #endif
        }
    } else {
        slave_pairing_hold_start = 0;
        slave_pairing_triggered = false;
    }
}
#endif // IS_MASTER

// ============================================================================
// Slave: Pairing shortcut detection
// ============================================================================

#if IS_SLAVE

static uint32_t slave_repair_hold_start = 0;
static bool slave_repair_triggered = false;

/**
 * @brief Check for pairing shortcuts held on the local matrix (slave)
 *
 * BACKSPACE + SPACE (5s) → Enter pairing mode / re-discover master
 */
static void check_slave_pairing_shortcuts(const matrix_state_t* state) {
    // Changed from Backspace (COL7) to Key 7 (COL0) to ensure it works on ESP32-C3 
    // which only has 6 physical columns. Key 7 is ROW0, COL0. Space is ROW4, COL0.
    bool key7_pressed = (state->rows[0] & (1 << 0)) != 0;
    bool space_pressed = (state->rows[4] & (1 << 0)) != 0;

    uint32_t now = hal::millis_now();

    if (key7_pressed && space_pressed) {
        if (slave_repair_hold_start == 0) {
            slave_repair_hold_start = now;
        } else if (!slave_repair_triggered &&
                   (now - slave_repair_hold_start) >= PAIRING_SHORTCUT_HOLD_MS) {
            slave_repair_triggered = true;
            DBG_PRINTLN(F("[PAIR] Slave shortcut triggered: entering pairing mode"));
            #if ENABLE_SPLIT_LINK
            split_link_reset_pairing();
            #endif
        }
    } else {
        slave_repair_hold_start = 0;
        slave_repair_triggered = false;
    }
}
#endif // IS_SLAVE

// ============================================================================
// Pairing mode timeout check (master only)
// ============================================================================

#if IS_MASTER
/**
 * @brief Check if pairing mode should end
 *
 * Exits pairing mode when:
 *  - The 60-second timeout expires
 *  - PC BLE mode: a host connects
 *  - Slave search mode: a slave is discovered
 */
static void check_pairing_mode_timeout() {
    if (pairing_mode == PAIRING_NONE) return;

    uint32_t now = hal::millis_now();

    // Check timeout (60s)
    if ((now - pairing_start_time) >= PAIRING_MODE_TIMEOUT_MS) {
        DBG_PRINTLN(F("[PAIR] Pairing mode timeout — resuming normal operation"));
        pairing_mode = PAIRING_NONE;
        #if ENABLE_LED
        hal::led_off();
        #endif
        return;
    }

    // PC BLE mode exits when a host connects
    if (pairing_mode == PAIRING_PC_BLE) {
        #if ENABLE_BLE_HID
        if (ble_hid_is_connected()) {
            DBG_PRINTLN(F("[PAIR] BLE host connected — exiting pairing mode"));
            pairing_mode = PAIRING_NONE;
            #if ENABLE_LED
            hal::led_off();
            #endif
        }
        #endif
    }

    // Slave search mode exits when a slave is discovered
    if (pairing_mode == PAIRING_SLAVE_SEARCH) {
        #if ENABLE_SPLIT_LINK
        if (split_link_is_paired()) {
            DBG_PRINTLN(F("[PAIR] Slave connected — exiting pairing mode"));
            pairing_mode = PAIRING_NONE;
            #if ENABLE_LED
            hal::led_off();
            #endif
        }
        #endif
    }
}
#endif // IS_MASTER

// ============================================================================
// LED blink logic
// ============================================================================

static void update_led(uint32_t now) {
    #if ENABLE_LED && LED_PIN != 0xFF
    #if IS_MASTER
    // Master: blink LED during pairing mode, off otherwise
    if (pairing_mode != PAIRING_NONE) {
        if ((now - last_led_toggle_time) >= LED_BLINK_INTERVAL_MS) {
            last_led_toggle_time = now;
            led_blink_state = !led_blink_state;
            if (led_blink_state) {
                hal::led_on();
            } else {
                hal::led_off();
            }
        }
    }
    #else
    // Slave: blink while searching, solid on when connected
    if (!split_link_is_paired()) {
        if ((now - last_led_toggle_time) >= LED_BLINK_INTERVAL_MS) {
            last_led_toggle_time = now;
            led_blink_state = !led_blink_state;
            if (led_blink_state) {
                hal::led_on();
            } else {
                hal::led_off();
            }
        }
    } else {
        // Connected — LED solid on
        if (!led_blink_state) {
            led_blink_state = true;
            hal::led_on();
        }
    }
    #endif
    #endif // ENABLE_LED && LED_PIN != 0xFF
}

// ============================================================================
// setup()
// ============================================================================

void setup() {
    // 1. Initialize HAL (GPIO, serial, etc.)
    hal::init();

    // 2. Initialize onboard LED
    #if ENABLE_LED
    hal::led_init();
    #endif

    // 3. Initialize power management (checks wake reason)
    power_init();

    // 4. Initialize key matrix scanner
    matrix_init();

    #if IS_MASTER
    // ---- Master-specific initialization ----

    // 5. Initialize wireless for split link
    // WiFi initialization is handled entirely within split_link_init()
    // WiFi UDP transport handles its own WiFi init inside split_link_init()

    // 6. Initialize split link (auto-discovery)
    #if ENABLE_SPLIT_LINK
    split_link_init();
    #endif

    // 7. Initialize BLE HID keyboard
    #if ENABLE_BLE_HID
    ble_hid_init();
    #endif

    DBG_PRINTLN(F("\n[MAIN] Master node ready!"));
    DBG_PRINTLN(F("[MAIN] Auto-discovery active — waiting for slaves..."));

    #else
    // ---- Slave-specific initialization ----

    // 5. Initialize wireless for split link
    // WiFi initialization is handled entirely within split_link_init()
    // WiFi UDP transport handles its own WiFi init inside split_link_init()

    // 6. Initialize split link (auto-discovery)
    #if ENABLE_SPLIT_LINK
    split_link_init();
    #endif

    DBG_PRINTLN(F("\n[MAIN] Slave node ready!"));
    DBG_PRINTLN(F("[MAIN] Auto-discovery active — searching for master..."));

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
    // --- Step 1.5: Check pairing shortcuts ---
    check_pairing_shortcuts(matrix_get_state());

    // --- Step 1.6: Check pairing mode timeout ---
    check_pairing_mode_timeout();

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
    // --- Step 1.5: Check slave pairing shortcuts ---
    check_slave_pairing_shortcuts(matrix_get_state());

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

    // --- Step 4: Update status LED ---
    update_led(now);

    // --- Step 5: Check for idle timeout → deep sleep ---
    // Inhibit sleep during pairing mode (master) or discovery (slave)
    #if ENABLE_DEEPSLEEP
    {
        #if IS_MASTER
        bool inhibit_sleep = (pairing_mode != PAIRING_NONE);
        #else
        bool inhibit_sleep = !split_link_is_paired();
        #endif
        if (!inhibit_sleep) {
            power_check_sleep(matrix_last_activity());
        }
    }
    #endif

    // --- Step 6: Yield / small delay ---
    // delayMicroseconds is handled by the scan interval
    hal::delay_us(MATRIX_SCAN_INTERVAL_US);
}


