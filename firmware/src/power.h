/**
 * @file power.h
 * @brief C3-Cascade — Power management (deep sleep & wake)
 *
 * Manages idle detection and deep sleep entry/exit.
 */

#ifndef C3CASCADE_POWER_H
#define C3CASCADE_POWER_H

#include "config.h"
#include <stdint.h>

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize power management
 * Checks wake reason and logs boot info.
 */
void power_init();

/**
 * @brief Check if we should enter deep sleep and do so if needed
 * Call this at the end of each loop iteration.
 * @param last_activity_ms  millis() timestamp of last key/slave activity
 */
void power_check_sleep(uint32_t last_activity_ms);

/**
 * @brief Force immediate deep sleep (e.g., from a keymap action)
 */
void power_force_sleep();

/**
 * @brief Check if the current boot was a wake from deep sleep
 */
bool power_was_wake();

/**
 * @brief Get the number of boots since power-on (survives deep sleep)
 */
uint32_t power_boot_count();

#endif // C3CASCADE_POWER_H
