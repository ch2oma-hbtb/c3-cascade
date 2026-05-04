/**
 * @file hal_nrf52.h
 * @brief C3-Cascade — nRF52840 specific HAL declarations (skeleton)
 *
 * Placeholder for nRF52840-specific functions.
 * Implementation will use the Adafruit nRF52 Arduino core.
 */

#ifndef C3CASCADE_HAL_NRF52_H
#define C3CASCADE_HAL_NRF52_H

#if defined(BOARD_NRF52840)

#include "hal.h"

namespace hal {
namespace nrf52 {

/**
 * @brief Initialize BLE using Adafruit Bluefruit library
 */
void ble_init();

/**
 * @brief Configure GPIO sense for system-off wake
 *
 * nRF52840 uses the SENSE mechanism on GPIO pins to wake from
 * System OFF mode. Each column pin is configured with SENSE LOW.
 */
void configure_sense_pins();

} // namespace nrf52
} // namespace hal

#endif // BOARD_NRF52840
#endif // C3CASCADE_HAL_NRF52_H
