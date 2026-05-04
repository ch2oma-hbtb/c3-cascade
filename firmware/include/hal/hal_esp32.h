/**
 * @file hal_esp32.h
 * @brief C3-Cascade — ESP32 (C3/C6) specific HAL declarations
 *
 * Additional ESP32-specific functions beyond the generic HAL interface.
 */

#ifndef C3CASCADE_HAL_ESP32_H
#define C3CASCADE_HAL_ESP32_H

#if defined(BOARD_XIAO_ESP32C3) || defined(BOARD_XIAO_ESP32C6)

#include "hal.h"

namespace hal {
namespace esp32 {

/**
 * @brief Initialize Wi-Fi in STA mode for ESP-NOW
 * Does NOT connect to an AP — just enables the radio.
 */
void wifi_init_sta();

/**
 * @brief Set the Wi-Fi channel (for ESP-NOW)
 */
void wifi_set_channel(uint8_t channel);

/**
 * @brief Prepare row pins for deep sleep wake
 *
 * Before entering deep sleep, all row pins must be driven LOW
 * so that pressing any key pulls a column pin LOW (triggering wake).
 */
void prepare_rows_for_sleep();

} // namespace esp32
} // namespace hal

#endif // ESP32 boards
#endif // C3CASCADE_HAL_ESP32_H
