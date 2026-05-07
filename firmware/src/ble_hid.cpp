/**
 * @file ble_hid.cpp
 * @brief C3-Cascade — BLE HID Keyboard service implementation
 *
 * Uses NimBLE stack for ESP32-C3/C6.
 * Implements a standard Boot Keyboard HID device with:
 *   - HID Service (UUID 0x1812)
 *   - HID Report characteristic (Input Report)
 *   - HID Report Map
 *   - Device Information Service
 *   - Battery Service (optional)
 */

#include <Arduino.h>

#include "ble_hid.h"
#include "config.h"
#include "hal/hal.h"

// ============================================================================
// ESP32 (NimBLE) implementation
// ============================================================================
// Note: Pico 2W uses ble_hid_btstack.cpp instead
#if HAS_NIMBLE

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLECharacteristic.h>

// HID Report Descriptor — Standard Boot Keyboard
// Reference: USB HID Usage Tables, Device Class Definition for HID 1.11
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

    // LED output report (for Caps Lock, Num Lock, etc.)
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

// NimBLE objects
static NimBLEServer*          pServer         = nullptr;
static NimBLEHIDDevice*       pHIDDevice      = nullptr;
static NimBLECharacteristic*  pInputReport    = nullptr;
static bool                   connected       = false;

// ============================================================================
// BLE Server Callbacks (NimBLE-Arduino 1.4.x API)
// ============================================================================

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server) override {
        connected = true;
        DBG_PRINTLN(F("[BLE] Client connected"));
    }

    void onDisconnect(NimBLEServer* server) override {
        connected = false;
        DBG_PRINTLN(F("[BLE] Client disconnected"));

        // Restart advertising
        NimBLEDevice::startAdvertising();
        DBG_PRINTLN(F("[BLE] Advertising restarted"));
    }

    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if (desc->sec_state.encrypted) {
            DBG_PRINTLN(F("[BLE] Pairing successful (encrypted)"));
        } else {
            DBG_PRINTLN(F("[BLE] WARNING: Connection not encrypted!"));
        }
    }
};

// ============================================================================
// Initialization
// ============================================================================

void ble_hid_init() {
    DBG_PRINTLN(F("[BLE] Initializing NimBLE HID Keyboard..."));

    // Initialize NimBLE
    NimBLEDevice::init(BLE_DEVICE_NAME);

    // Set security — enable bonding and SC
    NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, MITM, SC
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // Create server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Create HID Device
    pHIDDevice = new NimBLEHIDDevice(pServer);

    // Set HID report descriptor
    pHIDDevice->reportMap(
        (uint8_t*)HID_REPORT_DESCRIPTOR,
        sizeof(HID_REPORT_DESCRIPTOR)
    );

    // Get the Input Report characteristic (Report ID 1)
    pInputReport = pHIDDevice->inputReport(1);

    // Set device information
    pHIDDevice->manufacturer(BLE_MANUFACTURER);
    pHIDDevice->pnp(
        0x02,       // Vendor ID source (USB Implementer's Forum)
        0x05AC,     // Vendor ID (placeholder)
        0x820A,     // Product ID (placeholder)
        0x0001      // Product version
    );

    // HID Information: flags = 0x01 (RemoteWake), country = 0x00
    pHIDDevice->hidInfo(0x00, 0x01);

    #if ENABLE_BATTERY_REPORT
    // Battery service is created by NimBLEHIDDevice automatically
    pHIDDevice->setBatteryLevel(100);
    #endif

    // Start the HID service
    pHIDDevice->startServices();

    // Configure advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(0x03C1);  // Keyboard
    pAdvertising->addServiceUUID(pHIDDevice->hidService()->getUUID());
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);

    // Start advertising
    NimBLEDevice::startAdvertising();

    DBG_PRINTLN(F("[BLE] HID Keyboard service started"));
    DBG_PRINTLN(F("[BLE] Advertising... waiting for connection"));
}

// ============================================================================
// Report sending
// ============================================================================

bool ble_hid_send_report(const hid_keyboard_report_t* report) {
    if (!connected || pInputReport == nullptr) {
        return false;
    }

    // Send the 8-byte keyboard report
    pInputReport->setValue((uint8_t*)report, sizeof(hid_keyboard_report_t));
    pInputReport->notify();

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
    return connected;
}

void ble_hid_shutdown() {
    DBG_PRINTLN(F("[BLE] Shutting down..."));

    if (connected) {
        ble_hid_release_all();
        // Small delay to ensure the release report is sent
        delay(50);
    }

    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);

    connected = false;
    DBG_PRINTLN(F("[BLE] Shutdown complete"));
}

void ble_hid_set_battery_level(uint8_t level) {
    #if ENABLE_BATTERY_REPORT
    if (pHIDDevice != nullptr) {
        pHIDDevice->setBatteryLevel(level);
    }
    #endif
    (void)level; // Suppress unused warning when disabled
}

// ============================================================================
// nRF52840 implementation (skeleton)
// ============================================================================
#elif defined(BOARD_NRF52840)

// TODO: Implement using Adafruit Bluefruit HID library
// #include <bluefruit.h>
// BLEHidAdafruit blehid;

void ble_hid_init() {
    DBG_PRINTLN(F("[BLE] nRF52840 HID init — not implemented yet"));
}

bool ble_hid_send_report(const hid_keyboard_report_t* report) {
    (void)report;
    return false;
}

void ble_hid_release_all() {}

bool ble_hid_is_connected() {
    return false;
}

void ble_hid_shutdown() {}

void ble_hid_set_battery_level(uint8_t level) {
    (void)level;
}

#endif // Board implementations
