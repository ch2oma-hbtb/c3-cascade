/**
 * @file hal_esp32.cpp
 * @brief C3-Cascade — ESP32 (C3/C6) HAL implementation
 *
 * Implements the generic hal:: interface and esp32-specific extensions
 * using Arduino + ESP-IDF APIs.
 */

#if defined(BOARD_XIAO_ESP32C3) || defined(BOARD_XIAO_ESP32C6)

#include "config.h"
#include "pins.h"
#include "hal/hal.h"
#include "hal/hal_esp32.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_bt.h>

// RTC-persistent flag to track if we just woke from Light Sleep
RTC_DATA_ATTR static bool woke_from_light_sleep_flag = false;

// ============================================================================
// hal:: generic interface
// ============================================================================

void hal::init() {
    // Initialize serial for debug output
    #if DEBUG_SERIAL
    Serial.begin(115200);
    delay(100);
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

    // On ESP32-C3, rows might have been "held" during sleep. 
    // Release them so matrix scanning can work.
    for (int i = 0; i < MATRIX_ROWS; i++) {
        gpio_hold_dis((gpio_num_t)ROW_PINS[i]);
    }
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
            pinMode(pin, OUTPUT_OPEN_DRAIN);
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
    DBG_PRINTLN(F("[POWER] Preparing for sleep..."));

    // Drive all row pins LOW so key presses pull columns LOW
    hal::esp32::prepare_rows_for_sleep();

    #if defined(BOARD_XIAO_ESP32C3)
    // On ESP32-C3, we use Light Sleep because non-RTC row pins 
    // lose their state in Deep Sleep, preventing any-key wakeup.
    
    // 1. Force stop all radio activity
    esp_wifi_stop();
    esp_bt_controller_disable();

    // 2. Configure Columns for wakeup using low-level ESP-IDF API
    gpio_config_t conf;
    conf.pin_bit_mask = 0;
    for (int i = 0; i < NUM_PHYSICAL_COLS; i++) {
        conf.pin_bit_mask |= (1ULL << COL_PINS[i]);
    }
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_LOW_LEVEL; // Required for gpio_wakeup_enable
    gpio_config(&conf);

    // 3. Enable wakeup for each column
    for (int i = 0; i < NUM_PHYSICAL_COLS; i++) {
        gpio_wakeup_enable((gpio_num_t)COL_PINS[i], GPIO_INTR_LOW_LEVEL);
    }
    
    // 4. Enable the global GPIO wakeup source
    esp_sleep_enable_gpio_wakeup();

    DBG_PRINTLN(F("[POWER] Entering Light Sleep. Press any key to wake."));
    #if DEBUG_SERIAL
    Serial.flush();
    delay(10);
    #endif

    // 5. Start Light Sleep
    esp_light_sleep_start();
    
    // Set flag and reset to perform a clean re-init like Deep Sleep
    woke_from_light_sleep_flag = true;
    hal::system_reset();

    #else
    // On other boards (like C6), use Deep Sleep if possible
    #if defined(BOARD_XIAO_ESP32C6)
    esp_deep_sleep_enable_gpio_wakeup(
        wakeup_pin_mask,
        ESP_GPIO_WAKEUP_GPIO_LOW
    );
    #endif

    DBG_PRINTLN(F("[POWER] Entering Deep Sleep. Press any key to wake."));
    #if DEBUG_SERIAL
    Serial.flush();
    delay(10);
    #endif
    esp_deep_sleep_start();
    #endif
}

bool hal::woke_from_deep_sleep() {
    if (woke_from_light_sleep_flag) {
        woke_from_light_sleep_flag = false; // Clear for next time
        return true;
    }
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return (cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_ALL);
}

void hal::get_mac_address(uint8_t mac[6]) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

void hal::system_reset() {
    ESP.restart();
}

void hal::led_init() {
    #if ENABLE_LED && LED_PIN != 0xFF
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    #endif
}

void hal::led_on() {
    #if ENABLE_LED && LED_PIN != 0xFF
    digitalWrite(LED_PIN, HIGH);
    #endif
}

void hal::led_off() {
    #if ENABLE_LED && LED_PIN != 0xFF
    digitalWrite(LED_PIN, LOW);
    #endif
}

// ============================================================================
// hal::esp32:: specific functions
// ============================================================================

void hal::esp32::wifi_init_sta() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Don't connect to any AP
    DBG_PRINTLN(F("[HAL] Wi-Fi initialized in STA mode (for ESP-NOW)"));

    // Print MAC address for pairing
    uint8_t mac[6];
    hal::get_mac_address(mac);
    DBG_PRINT("[HAL] MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void hal::esp32::wifi_set_channel(uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void hal::esp32::prepare_rows_for_sleep() {
    // Set all row pins to OUTPUT LOW
    // When a key is pressed during sleep, it connects a row (LOW) to a column,
    // pulling the column LOW and triggering the GPIO wake interrupt.
    for (int i = 0; i < MATRIX_ROWS; i++) {
        pinMode(ROW_PINS[i], OUTPUT);
        digitalWrite(ROW_PINS[i], LOW);
        // On ESP32-C3, non-RTC pins (like D6-D10) need an explicit hold 
        // to stay LOW during sleep.
        gpio_hold_en((gpio_num_t)ROW_PINS[i]);
    }

    // Ensure column pins are INPUT_PULLUP (they're the wake sources)
    for (int i = 0; i < NUM_PHYSICAL_COLS; i++) {
        pinMode(COL_PINS[i], INPUT_PULLUP);
    }
}

#endif // ESP32 boards
