/**
 * @file matrix.cpp
 * @brief C3-Cascade — Key matrix scanner implementation
 *
 * Classic row-driven scan:
 *   1. Set one ROW pin LOW at a time (all others HIGH-Z)
 *   2. Read all COL pins (LOW = pressed, HIGH = released)
 *   3. Apply per-key debounce
 *   4. Track state changes and last-activity time
 */

#include <Arduino.h>
#include <string.h>

#include "matrix.h"
#include "config.h"
#include "pins.h"
#include "hal/hal.h"

// ============================================================================
// Internal state
// ============================================================================

// Raw (unfiltered) matrix read
static matrix_state_t raw_state;

// Debounced (stable) matrix state
static matrix_state_t debounced_state;

// Previous debounced state (for change detection)
static matrix_state_t previous_state;

// Per-key debounce timers
static uint32_t debounce_timer[MATRIX_ROWS][MATRIX_COLS];

// Per-key pending state during debounce
static uint8_t pending_state[MATRIX_ROWS][MATRIX_COLS];

// Timestamp of the last key activity
static uint32_t last_activity_time;

static void restore_row_outputs_high() {
    for (int i = 0; i < MATRIX_ROWS; i++) {
        hal::gpio_set_mode(ROW_PINS[i], hal::PIN_OUTPUT);
        hal::gpio_write(ROW_PINS[i], 1);
    }
}

static bool read_row_to_row_key(int drive_row, int sense_row) {
    for (int i = 0; i < MATRIX_ROWS; i++) {
        hal::gpio_set_highz(ROW_PINS[i]);
    }

    hal::gpio_set_mode(ROW_PINS[sense_row], hal::PIN_INPUT_PULLUP);
    hal::gpio_set_mode(ROW_PINS[drive_row], hal::PIN_OUTPUT);
    hal::gpio_write(ROW_PINS[drive_row], 0);
    hal::delay_us(50);

    bool pressed = (hal::gpio_read(ROW_PINS[sense_row]) == 0);
    restore_row_outputs_high();
    return pressed;
}

// ============================================================================
// Initialization
// ============================================================================

void matrix_init() {
    // Configure row pins as OUTPUT, initially HIGH (inactive)
    for (int r = 0; r < MATRIX_ROWS; r++) {
        hal::gpio_set_mode(ROW_PINS[r], hal::PIN_OUTPUT);
        hal::gpio_write(ROW_PINS[r], 1);  // HIGH = inactive
    }

    // Configure physical column pins as INPUT_PULLUP (Columns 0-5)
    for (int c = 0; c < 6; c++) {
        hal::gpio_set_mode(COL_PINS[c], hal::PIN_INPUT_PULLUP);
    }

    // Clear all state
    memset(&raw_state, 0, sizeof(raw_state));
    memset(&debounced_state, 0, sizeof(debounced_state));
    memset(&previous_state, 0, sizeof(previous_state));
    memset(debounce_timer, 0, sizeof(debounce_timer));
    memset(pending_state, 0, sizeof(pending_state));

    last_activity_time = hal::millis_now();

    DBG_PRINTLN(F("[MATRIX] Initialized"));
    DBG_PRINT("[MATRIX] Size: %d rows × %d cols = %d keys\n",
              MATRIX_ROWS, MATRIX_COLS, MAX_KEYS);
}

// ============================================================================
// Scan
// ============================================================================

/**
 * @brief Read the raw matrix state (no debounce)
 */
static void matrix_read_raw() {
    for (int r = 0; r < MATRIX_ROWS; r++) {
        // Activate this row (drive LOW)
        hal::gpio_write(ROW_PINS[r], 0);

        // Small delay for signal to settle (especially with longer traces)
        hal::delay_us(5);

        // Read all columns
        uint8_t row_bits = 0;
        // Standard Columns (0-5)
        for (int c = 0; c < 6; c++) {
            if (hal::gpio_read(COL_PINS[c]) == 0) {
                row_bits |= (1 << c);
            }
        }

        raw_state.rows[r] = row_bits;
        hal::gpio_write(ROW_PINS[r], 1); // Deactivate
    }

    // Extra scan phase: these keys are wired between row pins, so we treat
    // them as a virtual COL6 by temporarily using one row as an input.
    if (read_row_to_row_key(1, 0) || read_row_to_row_key(0, 1)) {
        raw_state.rows[1] |= (1 << 6);
    }

    if (read_row_to_row_key(2, 3) || read_row_to_row_key(3, 2)) {
        raw_state.rows[2] |= (1 << 6);
    }
}

/**
 * @brief Apply per-key debounce to raw state
 * @return true if debounced state changed
 */
static bool matrix_debounce() {
    uint32_t now = hal::millis_now();
    bool changed = false;

    for (int r = 0; r < MATRIX_ROWS; r++) {
        for (int c = 0; c < MATRIX_COLS; c++) {
            uint8_t raw_bit = (raw_state.rows[r] >> c) & 1;
            uint8_t debounced_bit = (debounced_state.rows[r] >> c) & 1;

            if (raw_bit != debounced_bit) {
                // Key state differs from debounced — start/continue debounce
                if (raw_bit != pending_state[r][c]) {
                    // New pending state — reset timer
                    pending_state[r][c] = raw_bit;
                    debounce_timer[r][c] = now;
                } else if ((now - debounce_timer[r][c]) >= DEBOUNCE_MS) {
                    // Pending state has been stable for DEBOUNCE_MS
                    if (raw_bit) {
                        debounced_state.rows[r] |= (1 << c);
                    } else {
                        debounced_state.rows[r] &= ~(1 << c);
                    }
                    changed = true;
                }
            } else {
                // Matches debounced state — reset pending
                pending_state[r][c] = debounced_bit;
            }
        }
    }

    return changed;
}

bool matrix_scan() {
    // Save previous state for change detection
    memcpy(&previous_state, &debounced_state, sizeof(matrix_state_t));

    // Read raw state from hardware
    matrix_read_raw();

    // Apply debounce
    bool changed = matrix_debounce();

    // Update activity timestamp
    if (changed) {
        last_activity_time = hal::millis_now();

        #if DEBUG_MATRIX
        matrix_debug_print();
        #endif
    }

    return changed;
}

// ============================================================================
// Getters
// ============================================================================

const matrix_state_t* matrix_get_state() {
    return &debounced_state;
}

bool matrix_any_key_pressed() {
    for (int r = 0; r < MATRIX_ROWS; r++) {
        if (debounced_state.rows[r] != 0) {
            return true;
        }
    }
    return false;
}

uint32_t matrix_last_activity() {
    return last_activity_time;
}

void matrix_reset_activity() {
    last_activity_time = hal::millis_now();
}

// ============================================================================
// Debug
// ============================================================================

void matrix_debug_print() {
    #if DEBUG_SERIAL
    DBG_PRINTLN(F("[MATRIX] Current state:"));
    for (int r = 0; r < MATRIX_ROWS; r++) {
        DBG_PRINT("  ROW%d: ", r);
        for (int c = 0; c < MATRIX_COLS; c++) {
            DBG_PRINT("%c ", (debounced_state.rows[r] & (1 << c)) ? 'X' : '.');
        }
        DBG_PRINT("\n");
    }
    #endif
}
