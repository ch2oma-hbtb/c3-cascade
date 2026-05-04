/**
 * @file hal.h
 * @brief C3-Cascade — Hardware Abstraction Layer interface
 *
 * Provides a unified API for GPIO, timing, and power management
 * across all supported MCUs. Each MCU implements this interface
 * in its own hal_xxx.cpp file.
 */

#ifndef C3CASCADE_HAL_H
#define C3CASCADE_HAL_H

#include <stdint.h>

namespace hal {

// ============================================================================
// Pin modes
// ============================================================================
enum PinMode {
    PIN_INPUT,
    PIN_INPUT_PULLUP,
    PIN_INPUT_PULLDOWN,
    PIN_OUTPUT,
    PIN_OUTPUT_OPEN_DRAIN,
};

// ============================================================================
// GPIO
// ============================================================================

/**
 * @brief Initialize the HAL (clocks, GPIO, serial)
 */
void init();

/**
 * @brief Set a GPIO pin mode
 */
void gpio_set_mode(uint8_t pin, PinMode mode);

/**
 * @brief Read a GPIO pin (returns 0 or 1)
 */
int gpio_read(uint8_t pin);

/**
 * @brief Write to a GPIO pin (0 = LOW, 1 = HIGH)
 */
void gpio_write(uint8_t pin, uint8_t value);

/**
 * @brief Set pin to high-impedance (INPUT with no pull)
 */
void gpio_set_highz(uint8_t pin);

// ============================================================================
// Timing
// ============================================================================

/**
 * @brief Milliseconds since boot (wraps after ~49 days)
 */
uint32_t millis_now();

/**
 * @brief Microseconds since boot
 */
uint32_t micros_now();

/**
 * @brief Delay in milliseconds
 */
void delay_ms(uint32_t ms);

/**
 * @brief Delay in microseconds
 */
void delay_us(uint32_t us);

// ============================================================================
// Power management
// ============================================================================

/**
 * @brief Enter deep sleep with GPIO wake capability
 * @param wakeup_pin_mask  Bitmask of GPIO pins that can wake the MCU
 *                         (interpretation is MCU-specific)
 */
void enter_deep_sleep(uint64_t wakeup_pin_mask);

/**
 * @brief Check if the current boot was caused by a wake from deep sleep
 * @return true if woke from deep sleep
 */
bool woke_from_deep_sleep();

// ============================================================================
// System
// ============================================================================

/**
 * @brief Get the unique MAC address of this board (6 bytes)
 */
void get_mac_address(uint8_t mac[6]);

/**
 * @brief Software reset
 */
void system_reset();

} // namespace hal

#endif // C3CASCADE_HAL_H
