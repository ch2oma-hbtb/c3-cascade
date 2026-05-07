/**
 * @file ble_hid_btstack.cpp
 * @brief C3-Cascade — BLE HID Keyboard for Pico 2W (BTstack via arduino-pico)
 *
 * Implements the BLE HID keyboard service for RP2040/RP2350 using
 * the Earle Philhower arduino-pico core's built-in Bluetooth support.
 *
 * The arduino-pico core provides PicoBluetoothBLEHID which wraps
 * BTstack's HID-over-GATT (HOG) implementation.
 *
 * Note: This file is only compiled for BOARD_PICO2_W builds.
 * ESP32 boards use ble_hid.cpp (NimBLE) instead.
 */

#if defined(BOARD_PICO2_W)

#include <Arduino.h>
#include <string.h>

#include "ble_hid.h"
#include "config.h"
#include "hal/hal.h"

// arduino-pico BLE HID support
#include <PicoBluetoothBLEHID.h>

// ============================================================================
// HID Report Descriptor — Standard Boot Keyboard
// Same descriptor as the NimBLE version for compatibility
// ============================================================================

static const uint8_t HID_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)

    // Modifier keys (8 bits)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    // Reserved byte
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant)

    // LED output report (Caps Lock, Num Lock, etc.)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x91, 0x02,       //   Output (Data, Variable, Absolute)

    // LED padding
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Constant)

    // Key codes (6 bytes)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array)

    0xC0              // End Collection
};

// BLE HID instance
static PicoBluetoothBLEHID ble_hid_instance;
static bool connected = false;
static bool ble_started = false;

// ============================================================================
// Initialization
// ============================================================================

void ble_hid_init() {
    DBG_PRINTLN(F("[BLE] Initializing BTstack HID Keyboard..."));

    // Start the BLE HID keyboard using the arduino-pico API
    ble_hid_instance.startHID(
        BLE_DEVICE_NAME,
        BLE_MANUFACTURER,
        0x03C1,             // Appearance: Keyboard
        HID_REPORT_DESCRIPTOR,
        sizeof(HID_REPORT_DESCRIPTOR),
        nullptr, 0          // No output report callback
    );

    ble_started = true;
    connected = false;

    DBG_PRINTLN(F("[BLE] BTstack HID Keyboard started"));
    DBG_PRINTLN(F("[BLE] Advertising... waiting for connection"));
}

// ============================================================================
// Report sending
// ============================================================================

bool ble_hid_send_report(const hid_keyboard_report_t* report) {
    if (!ble_started) {
        return false;
    }

    // Check connection state
    connected = ble_hid_instance.connected();
    if (!connected) {
        return false;
    }

    // Send the keyboard report (Report ID 1)
    // The report format is: modifiers(1) + reserved(1) + keys(6) = 8 bytes
    ble_hid_instance.sendReport(1, (uint8_t*)report, sizeof(hid_keyboard_report_t));

    #if DEBUG_BLE
    DBG_PRINT("[BLE] Report: mod=0x%02X keys=[", report->modifiers);
    for (int i = 0; i < HID_MAX_KEYS; i++) {
        DBG_PRINT("0x%02X ", report->keys[i]);
    }
    DBG_PRINT("]\n");
    #endif

    return true;
}

void ble_hid_release_all() {
    hid_keyboard_report_t empty_report;
    memset(&empty_report, 0, sizeof(empty_report));
    ble_hid_send_report(&empty_report);
}

bool ble_hid_is_connected() {
    if (ble_started) {
        connected = ble_hid_instance.connected();
    }
    return connected;
}

void ble_hid_shutdown() {
    DBG_PRINTLN(F("[BLE] Shutting down BTstack..."));

    if (connected) {
        ble_hid_release_all();
        delay(50);
    }

    if (ble_started) {
        ble_hid_instance.end();
        ble_started = false;
    }

    connected = false;
    DBG_PRINTLN(F("[BLE] Shutdown complete"));
}

void ble_hid_set_battery_level(uint8_t level) {
    #if ENABLE_BATTERY_REPORT
    if (ble_started) {
        ble_hid_instance.setBatteryLevel(level);
    }
    #endif
    (void)level;
}

#endif // BOARD_PICO2_W
