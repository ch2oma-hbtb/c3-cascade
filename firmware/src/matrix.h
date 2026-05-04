/**
 * @file matrix.h
 * @brief C3-Cascade — Key matrix scanner
 *
 * Handles row-driven matrix scanning with software debounce.
 * Produces a compact matrix state (bitfield) and tracks changes.
 */

#ifndef C3CASCADE_MATRIX_H
#define C3CASCADE_MATRIX_H

#include "config.h"
#include <stdint.h>

// ============================================================================
// Matrix state type
// ============================================================================

/**
 * Each row is a byte where each bit represents a column.
 * Bit set = key pressed.
 */
typedef struct {
    uint8_t rows[MATRIX_ROWS];  // One byte per row (bits 0..COLS-1)
} matrix_state_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the matrix scanner GPIO pins
 * Configures row pins as output and column pins as input with pull-ups.
 */
void matrix_init();

/**
 * @brief Perform a full matrix scan with debounce
 * @return true if the matrix state changed since last scan
 */
bool matrix_scan();

/**
 * @brief Get the current (debounced) matrix state
 */
const matrix_state_t* matrix_get_state();

/**
 * @brief Check if any key is currently pressed
 */
bool matrix_any_key_pressed();

/**
 * @brief Get the timestamp of the last key activity (press or release)
 * @return millis() value of last activity
 */
uint32_t matrix_last_activity();

/**
 * @brief Reset the activity timer (e.g., when receiving slave data)
 */
void matrix_reset_activity();

/**
 * @brief Print the current matrix state to Serial (debug)
 */
void matrix_debug_print();

#endif // C3CASCADE_MATRIX_H
