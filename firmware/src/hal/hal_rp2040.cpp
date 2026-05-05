/**
 * @file hal_rp2040.cpp
 * @brief C3-Cascade — RP2040/RP2350 (Pico 2W) HAL implementation
 *
 * Implements the generic hal:: interface using the Earle Philhower
 * arduino-pico core. Targets the Raspberry Pi Pico 2W (RP2350 + CYW43439).
 *
 * Key differences from ESP32:
 *   - No RTC-persistent memory (use watchdog scratch registers)
 *   - Deep sleep uses dormant mode with GPIO IRQ wake
 *   - WiFi via CYW43439 (no ESP-NOW support)
 *   - BLE via BTstack (no NimBLE)
 *   - MAC address read from CYW43439 chip
 */

#if defined(BOARD_PICO2_W)

#include "config.h"
#include "pins.h"
#include "hal/hal.h"
#include "hal/hal_rp2040.h"

#include <Arduino.h>
#include <WiFi.h>

// We use watchdog scratch registers to persist data across soft resets.
// Scratch[0] is used as a "woke from sleep" flag.
#include <hardware/watchdog.h>

// Magic value to detect wake-from-sleep reboot
#define SLEEP_WAKE_MAGIC    0xC3CA5C0D

// ============================================================================
// hal:: generic interface
// ============================================================================

void hal::init() {
    #if DEBUG_SERIAL
    Serial.begin(115200);

    // Wait for serial (USB CDC) — but with timeout so it works headless
    uint32_t serial_wait_start = millis();
    while (!Serial && (millis() - serial_wait_start) < 2000) {
        delay(10);
    }

    Serial.println();
    Serial.println(F("========================================"));
    Serial.print(F("C3-Cascade Firmware — "));
    Serial.println(BOARD_NAME);
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
            // RP2040 doesn't have true open-drain, but we can simulate
            // by switching between OUTPUT LOW and INPUT (high-Z)
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
    (void)wakeup_pin_mask;
    DBG_PRINTLN(F("[POWER] Preparing RP2350 for sleep..."));

    // 1. Prepare rows for sleep (drive LOW)
    hal::rp2040::prepare_rows_for_sleep();

    // 2. Set the wake-from-sleep flag in watchdog scratch register
    watchdog_hw->scratch[0] = SLEEP_WAKE_MAGIC;

    // 3. Configure column pins as GPIO IRQ wake sources (LOW level)
    // On RP2040/RP2350, we use dormant mode with GPIO interrupts.
    // The simplest approach: configure GPIO IRQs on column pins,
    // then enter dormant mode. On wake, the chip resets.

    // Set up column pins as wake sources
    for (int i = 0; i < MATRIX_COLS; i++) {
        pinMode(COL_PINS[i], INPUT_PULLUP);
    }

    DBG_PRINTLN(F("[POWER] Entering sleep. Press any key to wake."));
    #if DEBUG_SERIAL
    Serial.flush();
    delay(10);
    #endif

    // 4. Use the arduino-pico core's sleep/dormant support
    // For now, we use a simple approach: disable peripherals and
    // use a watchdog-triggered reboot when a key is pressed.
    //
    // Set up GPIO interrupt on any column pin going LOW
    // When triggered, it will cause a reboot via watchdog
    for (int i = 0; i < MATRIX_COLS; i++) {
        attachInterrupt(digitalPinToInterrupt(COL_PINS[i]), [](){
            // Any key press triggers a watchdog reset
            watchdog_reboot(0, 0, 0);
        }, FALLING);
    }

    // 5. Enter light sleep (WFI-based, ~2mA)
    // For deeper sleep (~150µA), use dormant mode with XOSC off
    // TODO: Implement full dormant mode using pico-sdk dormancy API
    while (true) {
        __wfi();  // Wait For Interrupt — ARM low-power idle
    }

    // Execution never reaches here — wake causes reboot
}

bool hal::woke_from_deep_sleep() {
    // Check if the watchdog scratch register has our magic value
    if (watchdog_hw->scratch[0] == SLEEP_WAKE_MAGIC) {
        watchdog_hw->scratch[0] = 0;  // Clear for next time
        return true;
    }
    return false;
}

void hal::get_mac_address(uint8_t mac[6]) {
    // Read MAC address from the CYW43439 WiFi chip
    // The arduino-pico core provides this via WiFi.macAddress()
    uint8_t wifi_mac[6];
    WiFi.macAddress(wifi_mac);
    memcpy(mac, wifi_mac, 6);
}

void hal::system_reset() {
    rp2040.reboot();
}

// ============================================================================
// hal::rp2040:: specific functions
// ============================================================================

void hal::rp2040::wifi_init_ap() {
    // WiFi AP init is handled by split_link_wifi_udp.cpp
    DBG_PRINTLN(F("[HAL] RP2350 WiFi AP mode — handled by split link"));
}

void hal::rp2040::wifi_init_sta() {
    // WiFi STA init is handled by split_link_wifi_udp.cpp
    DBG_PRINTLN(F("[HAL] RP2350 WiFi STA mode — handled by split link"));
}

void hal::rp2040::prepare_rows_for_sleep() {
    // Set all row pins to OUTPUT LOW
    // When a key is pressed during sleep, it connects a row (LOW)
    // to a column (PULLUP), pulling the column LOW and triggering wake.
    for (int i = 0; i < MATRIX_ROWS; i++) {
        pinMode(ROW_PINS[i], OUTPUT);
        digitalWrite(ROW_PINS[i], LOW);
    }

    // Ensure column pins are INPUT_PULLUP
    for (int i = 0; i < MATRIX_COLS; i++) {
        pinMode(COL_PINS[i], INPUT_PULLUP);
    }

    DBG_PRINTLN(F("[HAL] RP2350 row pins prepared for sleep"));
}

#endif // BOARD_PICO2_W
