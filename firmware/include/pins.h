/**
 * @file pins.h
 * @brief C3-Cascade — Per-MCU GPIO pin definitions
 *
 * Maps the keyboard matrix rows and columns to physical GPIO numbers
 * for each supported MCU board.
 *
 * Matrix wiring (from layout - primary.txt):
 *   ROW0 = D10,  ROW1 = D9,  ROW2 = D8,  ROW3 = D7,  ROW4 = D6
 *   COL0 = D0,   COL1 = D2,  COL2 = D5,  COL3 = D3,  COL4 = D4,  COL5 = D1
 *
 * Rows are outputs (active LOW during scan).
 * Columns are inputs with pull-ups (read LOW when key pressed).
 */

#ifndef C3CASCADE_PINS_H
#define C3CASCADE_PINS_H

#include "config.h"

// ============================================================================
// Seeed Studio XIAO ESP32-C3
// ============================================================================
#if defined(BOARD_XIAO_ESP32C3)

// Board pin label → GPIO mapping
//   D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO3, D4=GPIO4, D5=GPIO5
//   D6=GPIO6, D7=GPIO7, D8=GPIO8, D9=GPIO9, D10=GPIO10

static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    10,     // ROW0 → D10 → GPIO10
    9,      // ROW1 → D9  → GPIO9
    8,      // ROW2 → D8  → GPIO8
    20,     // ROW3 → D7  → GPIO20
    21,     // ROW4 → D6  → GPIO21
};

static const uint8_t COL_PINS[NUM_PHYSICAL_COLS] = {
    2,      // COL0 → D0 → GPIO2
    4,      // COL1 → D2 → GPIO4
    7,      // COL2 → D5 → GPIO7
    5,      // COL3 → D3 → GPIO5
    6,      // COL4 → D4 → GPIO6
    3,      // COL5 → D1 → GPIO3
};

// Deep sleep wake: only GPIO 0-5 are RTC-capable on ESP32-C3
// XIAO D0-D3 map to GPIO 2-5 (RTC-capable). D4-D5 (GPIO 6-7) are NOT RTC-capable.
#define DEEPSLEEP_WAKEUP_PIN_MASK   ( \
    (1ULL << 2) | (1ULL << 3) | (1ULL << 4) | (1ULL << 5) \
)

#define LED_PIN     0xFF     // No simple onboard LED (WS2812 on GPIO8)

// ============================================================================
// Seeed Studio XIAO ESP32-C6
// ============================================================================
#elif defined(BOARD_XIAO_ESP32C6)

// Board pin label → GPIO mapping (ESP32-C6 has different GPIO numbers!)
//   D0=GPIO0,  D1=GPIO1,  D2=GPIO2,   D3=GPIO21, D4=GPIO22
//   D5=GPIO23, D6=GPIO16, D7=GPIO17,  D8=GPIO19, D9=GPIO20, D10=GPIO18

static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    18,     // ROW0 → D10 → GPIO18
    20,     // ROW1 → D9  → GPIO20
    19,     // ROW2 → D8  → GPIO19
    17,     // ROW3 → D7  → GPIO17
    16,     // ROW4 → D6  → GPIO16
};

static const uint8_t COL_PINS[NUM_PHYSICAL_COLS] = {
    0,      // COL0 → D0 → GPIO0
    2,      // COL1 → D2 → GPIO2
    23,     // COL2 → D5 → GPIO23
    21,     // COL3 → D3 → GPIO21
    22,     // COL4 → D4 → GPIO22
    1,      // COL5 → D1 → GPIO1
};

// Deep sleep wake: ESP32-C6 — GPIO0-7 are LP (low-power) IO capable
// Columns GPIO0, GPIO1, GPIO2 are LP-capable
// For others, we use light-sleep GPIO wakeup or timer-based wakeup
#define DEEPSLEEP_WAKEUP_PIN_MASK   ( \
    (1ULL << 0) | (1ULL << 1) | (1ULL << 2) \
)

#define LED_PIN     0xFF     // No simple onboard LED (WS2812 on GPIO8)

// ============================================================================
// nRF52840 (placeholder — update for your actual PCB)
// ============================================================================
#elif defined(BOARD_NRF52840)

// Example using Adafruit Feather nRF52840 pin numbering
// These are PLACEHOLDER values — update to match your wiring!
static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    6,      // ROW0 → P0.06
    8,      // ROW1 → P0.08
    41,     // ROW2 → P1.09 (32 + 9)
    12,     // ROW3 → P0.12
    11,     // ROW4 → P0.11
};

static const uint8_t COL_PINS[NUM_PHYSICAL_COLS] = {
    13,     // COL0 → P0.13
    15,     // COL1 → P0.15
    17,     // COL2 → P0.17
    20,     // COL3 → P0.20
    22,     // COL4 → P0.22
    24,     // COL5 → P0.24
};

// nRF52840 uses NRF_GPIO sense mechanism for wake — handled in HAL
#define DEEPSLEEP_WAKEUP_PIN_MASK   0

#define LED_PIN     0xFF     // No onboard LED assigned

// ============================================================================
// Raspberry Pi Pico 2W (RP2350)
// ============================================================================
#elif defined(BOARD_PICO2_W)

// Pico 2W GPIO mapping
// Available GPIOs: GP0-GP22, GP26-GP28 (GP23-GP25, GP29 used internally)
// Using adjacent pins for easy PCB routing
static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    2,      // ROW0 → GP2
    3,      // ROW1 → GP3
    4,      // ROW2 → GP4
    5,      // ROW3 → GP5
    6,      // ROW4 → GP6
};

static const uint8_t COL_PINS[NUM_PHYSICAL_COLS] = {
    7,      // COL0 → GP7
    8,      // COL1 → GP8
    9,      // COL2 → GP9
    10,     // COL3 → GP10
    11,     // COL4 → GP11
    12,     // COL5 → GP12
    13,     // COL6 → GP13
};

// RP2040/RP2350 uses dormant mode — all GPIOs can trigger wake via IRQ
#define DEEPSLEEP_WAKEUP_PIN_MASK   0

#define LED_PIN     LED_BUILTIN     // CYW43 onboard LED

// ============================================================================
// Raspberry Pi Pico W (RP2040 + CYW4343W)
// ============================================================================
#elif defined(BOARD_PICO_W)

// Pico W GPIO mapping (from layout - slave.txt)
// ROW pins: GP28, GP21, GP20, GP19, GP18
// COL pins: GP4, GP3, GP2, GP1, GP0, GP17, GP27, GP22
// Note: GP0/GP1 are shared with UART0 TX/RX — USB CDC is used for debug instead
static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    28,     // ROW0 → GP28
    21,     // ROW1 → GP21
    20,     // ROW2 → GP20
    19,     // ROW3 → GP19
    18,     // ROW4 → GP18
};

static const uint8_t COL_PINS[NUM_PHYSICAL_COLS] = {
    4,      // COL0 → GP4
    3,      // COL1 → GP3
    2,      // COL2 → GP2
    1,      // COL3 → GP1
    0,      // COL4 → GP0
    17,     // COL5 → GP17
    27,     // COL6 → GP27
    22,     // COL7 → GP22
};

// RP2040 uses dormant mode — all GPIOs can trigger wake via IRQ
#define DEEPSLEEP_WAKEUP_PIN_MASK   0

#define LED_PIN     LED_BUILTIN     // CYW43 onboard LED

#endif // Board selection

#endif // C3CASCADE_PINS_H
