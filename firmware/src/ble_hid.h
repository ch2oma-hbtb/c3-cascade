/**
 * @file ble_hid.h
 * @brief C3-Cascade — BLE HID Keyboard service
 *
 * Implements a standard BLE HID keyboard using NimBLE (ESP32)
 * or Adafruit Bluefruit (nRF52840).
 */

#ifndef C3CASCADE_BLE_HID_H
#define C3CASCADE_BLE_HID_H

#include "config.h"
#include <stdint.h>

// ============================================================================
// HID Report structure (Boot Keyboard)
// ============================================================================

typedef struct {
    uint8_t modifiers;              // Modifier keys bitmask
    uint8_t reserved;               // Reserved (always 0x00)
    uint8_t keys[HID_MAX_KEYS];     // Up to 6 simultaneous key codes
} hid_keyboard_report_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize BLE and the HID Keyboard service
 * Sets up advertising, HID service, characteristics, and starts advertising.
 */
void ble_hid_init();

/**
 * @brief Send a keyboard report
 * @param report  Pointer to the report to send
 * @return true if the report was sent successfully
 */
bool ble_hid_send_report(const hid_keyboard_report_t* report);

/**
 * @brief Send an empty (all keys released) report
 */
void ble_hid_release_all();

/**
 * @brief Check if a BLE host is currently connected
 */
bool ble_hid_is_connected();

/**
 * @brief Stop BLE advertising and disconnect
 * Call before entering deep sleep.
 */
void ble_hid_shutdown();

/**
 * @brief Update battery level (if ENABLE_BATTERY_REPORT)
 * @param level  Battery percentage (0–100)
 */
void ble_hid_set_battery_level(uint8_t level);

#endif // C3CASCADE_BLE_HID_H
