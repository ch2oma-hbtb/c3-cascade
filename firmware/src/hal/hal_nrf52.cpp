/**
 * @file hal_nrf52.cpp
 * @brief C3-Cascade — nRF52840 HAL implementation (skeleton)
 *
 * Stub implementation for nRF52840 using Adafruit nRF52 Arduino core.
 * Most functions are placeholders — fill in when porting to nRF52840.
 */

#if defined(BOARD_NRF52840)

#include "config.h"
#include "pins.h"
#include "hal/hal.h"
#include "hal/hal_nrf52.h"

#include <Arduino.h>

// ============================================================================
// hal:: generic interface (nRF52840)
// ============================================================================

void hal::init() {
    #if DEBUG_SERIAL
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println(F("========================================"));
    Serial.println(F("C3-Cascade Firmware — nRF52840"));
    #if IS_MASTER
    Serial.println(F("Role: MASTER"));
    #else
    Serial.println(F("Role: SLAVE"));
    #endif
    Serial.println(F("========================================"));
    #endif
}

void hal::gpio_set_mode(uint8_t pin, hal::PinMode mode) {
    switch (mode) {
        case PIN_INPUT:
            pinMode(pin, INPUT);
            break;
        case PIN_INPUT_PULLUP:
            pinMode(pin, INPUT_PULLUP);
            break;
        case PIN_INPUT_PULLDOWN:
            pinMode(pin, INPUT_PULLDOWN);
            break;
        case PIN_OUTPUT:
            pinMode(pin, OUTPUT);
            break;
        case PIN_OUTPUT_OPEN_DRAIN:
            // nRF52 Arduino core may not support open-drain directly
            // Use standard output as fallback
            pinMode(pin, OUTPUT);
            break;
    }
}

int hal::gpio_read(uint8_t pin) {
    return digitalRead(pin);
}

void hal::gpio_write(uint8_t pin, uint8_t value) {
    digitalWrite(pin, value);
}

void hal::gpio_set_highz(uint8_t pin) {
    pinMode(pin, INPUT);
}

uint32_t hal::millis_now() {
    return millis();
}

uint32_t hal::micros_now() {
    return micros();
}

void hal::delay_ms(uint32_t ms) {
    delay(ms);
}

void hal::delay_us(uint32_t us) {
    delayMicroseconds(us);
}

void hal::enter_deep_sleep(uint64_t wakeup_pin_mask) {
    DBG_PRINTLN(F("[POWER] nRF52840 deep sleep — entering System OFF"));

    // Configure SENSE on column pins for wake
    hal::nrf52::configure_sense_pins();

    #if DEBUG_SERIAL
    Serial.flush();
    delay(10);
    #endif

    // nRF52840 System OFF mode
    // When a SENSE-enabled pin detects the configured level,
    // the chip resets and starts from the beginning of setup()
    // 
    // TODO: Implement using NRF_POWER->SYSTEMOFF = 1
    // For now, this is a placeholder:
    // NRF_POWER->SYSTEMOFF = 1;

    DBG_PRINTLN(F("[POWER] WARNING: nRF52840 System OFF not implemented yet"));
    while(1) { delay(1000); } // Halt as placeholder
}

bool hal::woke_from_deep_sleep() {
    // TODO: Check NRF_POWER->RESETREAS for GPIO wake
    // return (NRF_POWER->RESETREAS & POWER_RESETREAS_OFF_Msk);
    return false;
}

void hal::get_mac_address(uint8_t mac[6]) {
    // nRF52840 BLE address (from FICR)
    // TODO: Read from Bluefruit or FICR registers
    // Placeholder: fill with zeros
    memset(mac, 0, 6);
    DBG_PRINTLN(F("[HAL] WARNING: nRF52840 MAC address not implemented"));
}

void hal::system_reset() {
    NVIC_SystemReset();
}

// ============================================================================
// hal::nrf52:: specific functions
// ============================================================================

void hal::nrf52::ble_init() {
    // TODO: Initialize Adafruit Bluefruit BLE stack
    // Bluefruit.begin();
    // Bluefruit.setTxPower(4);
    // Bluefruit.setName(BLE_DEVICE_NAME);
    DBG_PRINTLN(F("[HAL] nRF52840 BLE init — not implemented yet"));
}

void hal::nrf52::configure_sense_pins() {
    // Drive all row pins LOW (same pattern as ESP32)
    for (int i = 0; i < MATRIX_ROWS; i++) {
        pinMode(ROW_PINS[i], OUTPUT);
        digitalWrite(ROW_PINS[i], LOW);
    }

    // Configure column pins with INPUT_PULLUP and SENSE LOW
    for (int i = 0; i < MATRIX_COLS; i++) {
        pinMode(COL_PINS[i], INPUT_PULLUP);
        // TODO: Configure NRF_GPIO SENSE for this pin
        // nrf_gpio_cfg_sense_input(COL_PINS[i], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    }

    DBG_PRINTLN(F("[HAL] nRF52840 SENSE pins configured (placeholder)"));
}

#endif // BOARD_NRF52840
