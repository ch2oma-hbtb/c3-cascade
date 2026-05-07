/**
 * @file hal_rp2040.h
 * @brief C3-Cascade — RP2040/RP2350 (Pico W / Pico 2W) specific HAL declarations
 *
 * Additional RP2040/RP2350-specific functions beyond the generic HAL interface.
 * Uses the Earle Philhower arduino-pico core.
 */

#ifndef C3CASCADE_HAL_RP2040_H
#define C3CASCADE_HAL_RP2040_H

#if defined(BOARD_PICO2_W)

#include "hal.h"

namespace hal {
namespace rp2040 {

/**
 * @brief Initialize Wi-Fi in AP mode (master)
 * Creates a SoftAP for slave nodes to connect to.
 * Called automatically by split_link_wifi_udp.cpp.
 */
void wifi_init_ap();

/**
 * @brief Initialize Wi-Fi in STA mode (slave)
 * Connects to the master's SoftAP.
 * Called automatically by split_link_wifi_udp.cpp.
 */
void wifi_init_sta();

/**
 * @brief Prepare row pins for dormant mode wake
 *
 * Before entering dormant mode, all row pins must be driven LOW
 * so that pressing any key pulls a column pin LOW (triggering wake).
 */
void prepare_rows_for_sleep();

} // namespace rp2040
} // namespace hal

#endif // BOARD_PICO2_W
#endif // C3CASCADE_HAL_RP2040_H
