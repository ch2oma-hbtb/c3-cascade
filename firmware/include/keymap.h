/**
 * @file keymap.h
 * @brief C3-Cascade — Keymap definitions (HID Usage IDs)
 *
 * Maps each matrix position [row][col] to a USB HID keyboard usage ID.
 * Reference: USB HID Usage Tables §10 Keyboard/Keypad Page (0x07)
 *
 * Layout (primary / left half):
 *
 *         COL0    COL1    COL2    COL3    COL4    COL5
 * ROW0    ESC     1       2       3       4       5
 * ROW1    TAB     Q       W       E       R       T
 * ROW2    CAPS    A       S       D       F       G
 * ROW3    LSHIFT  Z       X       C       V       B  (*)
 * ROW4    LCTRL   LGUI    LALT    ---     SPACE   ---
 *
 * (*) ROW3/COL5 = B (the layout shows "V, B" — V is at COL4, B at COL5)
 *     ROW0/COL5 = 5 (the layout shows "5, 6" — we map 5 here; 6 belongs to
 *                     the right half or an extra column)
 */

#ifndef C3CASCADE_KEYMAP_H
#define C3CASCADE_KEYMAP_H

#include "config.h"
#include <stdint.h>

// ============================================================================
// USB HID Keyboard Usage IDs (Page 0x07)
// ============================================================================

// Letters
#define HID_KEY_A               0x04
#define HID_KEY_B               0x05
#define HID_KEY_C               0x06
#define HID_KEY_D               0x07
#define HID_KEY_E               0x08
#define HID_KEY_F               0x09
#define HID_KEY_G               0x0A
#define HID_KEY_H               0x0B
#define HID_KEY_I               0x0C
#define HID_KEY_J               0x0D
#define HID_KEY_K               0x0E
#define HID_KEY_L               0x0F
#define HID_KEY_M               0x10
#define HID_KEY_N               0x11
#define HID_KEY_O               0x12
#define HID_KEY_P               0x13
#define HID_KEY_Q               0x14
#define HID_KEY_R               0x15
#define HID_KEY_S               0x16
#define HID_KEY_T               0x17
#define HID_KEY_U               0x18
#define HID_KEY_V               0x19
#define HID_KEY_W               0x1A
#define HID_KEY_X               0x1B
#define HID_KEY_Y               0x1C
#define HID_KEY_Z               0x1D

// Numbers
#define HID_KEY_1               0x1E
#define HID_KEY_2               0x1F
#define HID_KEY_3               0x20
#define HID_KEY_4               0x21
#define HID_KEY_5               0x22
#define HID_KEY_6               0x23
#define HID_KEY_7               0x24
#define HID_KEY_8               0x25
#define HID_KEY_9               0x26
#define HID_KEY_0               0x27

// Special keys
#define HID_KEY_ESC             0x29
#define HID_KEY_TAB             0x2B
#define HID_KEY_CAPS_LOCK       0x39
#define HID_KEY_SPACE           0x2C

// No key assigned
#define HID_KEY_NONE            0x00

// ============================================================================
// Modifier bit flags (for the HID modifier byte)
// ============================================================================
#define HID_MOD_LCTRL           0x01
#define HID_MOD_LSHIFT          0x02
#define HID_MOD_LALT            0x04
#define HID_MOD_LGUI            0x08    // Windows / Mac Command
#define HID_MOD_RCTRL           0x10
#define HID_MOD_RSHIFT          0x20
#define HID_MOD_RALT            0x40
#define HID_MOD_RGUI            0x80

// Special marker: this key is a modifier (upper byte = modifier bit)
#define MOD_KEY(mod)            (0xF000 | (mod))
#define IS_MOD_KEY(code)        (((code) & 0xF000) == 0xF000)
#define GET_MOD_BIT(code)       ((code) & 0x00FF)

// ============================================================================
// Layer 0 — Base layer (Primary / Left Half)
// ============================================================================

static const uint16_t KEYMAP_LAYER_0_PRIMARY[MATRIX_ROWS][MATRIX_COLS] = {
    //  COL0 (D0)           COL1 (D2)       COL2 (D5)       COL3 (D3)       COL4 (D4)       COL5 (D1)       COL6 (X-Wire)
    { HID_KEY_ESC,        HID_KEY_1,      HID_KEY_2,      HID_KEY_3,      HID_KEY_4,      HID_KEY_5,      HID_KEY_NONE    },  // ROW0 (D10)
    { HID_KEY_TAB,        HID_KEY_Q,      HID_KEY_W,      HID_KEY_E,      HID_KEY_R,      HID_KEY_T,      HID_KEY_6       },  // ROW1 (D9)
    { HID_KEY_CAPS_LOCK,  HID_KEY_A,      HID_KEY_S,      HID_KEY_D,      HID_KEY_F,      HID_KEY_G,      HID_KEY_B       },  // ROW2 (D8)
    { MOD_KEY(HID_MOD_LSHIFT), HID_KEY_NONE, HID_KEY_Z,      HID_KEY_X,      HID_KEY_C,      HID_KEY_V,      HID_KEY_NONE    },  // ROW3 (D7)
    { MOD_KEY(HID_MOD_LCTRL),  MOD_KEY(HID_MOD_LGUI), MOD_KEY(HID_MOD_LALT), HID_KEY_NONE, HID_KEY_SPACE, HID_KEY_NONE, HID_KEY_NONE },  // ROW4 (D6)
};

// ============================================================================
// Layer 0 — Base layer (Secondary / Right Half)
// ============================================================================

static const uint16_t KEYMAP_LAYER_0_SECONDARY[MATRIX_ROWS][MATRIX_COLS] = {
    //  COL0                COL1            COL2            COL3            COL4            COL5            COL6
    { HID_KEY_7,          HID_KEY_8,      HID_KEY_9,      HID_KEY_0,      HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE    },  // ROW0
    { HID_KEY_Y,          HID_KEY_U,      HID_KEY_I,      HID_KEY_O,      HID_KEY_P,      HID_KEY_NONE,   HID_KEY_NONE    },  // ROW1
    { HID_KEY_H,          HID_KEY_J,      HID_KEY_K,      HID_KEY_L,      HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE    },  // ROW2
    { HID_KEY_N,          HID_KEY_M,      HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,   MOD_KEY(HID_MOD_RSHIFT), HID_KEY_NONE }, // ROW3
    { HID_KEY_NONE,       HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE    },  // ROW4
};

// ============================================================================
// Layer tables
// ============================================================================
#define NUM_LAYERS  2

static const uint16_t (*KEYMAP_LAYERS_PRIMARY[NUM_LAYERS])[MATRIX_COLS] = {
    KEYMAP_LAYER_0_PRIMARY,
    KEYMAP_LAYER_0_PRIMARY, // Placeholder for FN
};

static const uint16_t (*KEYMAP_LAYERS_SECONDARY[NUM_LAYERS])[MATRIX_COLS] = {
    KEYMAP_LAYER_0_SECONDARY,
    KEYMAP_LAYER_0_SECONDARY, // Placeholder for FN
};

#endif // C3CASCADE_KEYMAP_H
