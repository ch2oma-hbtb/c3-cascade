/**
 * @file power.cpp
 * @brief C3-Cascade — Power management implementation
 *
 * Handles:
 *   - Idle detection (no key activity for DEEPSLEEP_TIMEOUT_MS)
 *   - Deep sleep entry with GPIO wake configuration
 *   - Boot count persistence across deep sleep cycles
 *   - Wake reason detection and logging
 */

#include <Arduino.h>

#include "power.h"
#include "config.h"
#include "pins.h"
#include "hal/hal.h"
#include "ble_hid.h"
#include "split_link.h"

// ============================================================================
// RTC-persistent data (survives deep sleep, not power-off)
// ============================================================================

#if defined(BOARD_XIAO_ESP32C3) || defined(BOARD_XIAO_ESP32C6)
RTC_DATA_ATTR static uint32_t boot_count = 0;
RTC_DATA_ATTR static bool was_sleeping = false;
#elif defined(BOARD_PICO2_W)
// RP2040/RP2350 has no RTC-persistent memory — use regular statics
// Wake detection uses watchdog scratch registers (see hal_rp2040.cpp)
static uint32_t boot_count = 0;
static bool was_sleeping = false;
#else
static uint32_t boot_count = 0;
static bool was_sleeping = false;
#endif

// ============================================================================
// Initialization
// ============================================================================

void power_init() {
    boot_count++;

    #if ENABLE_DEEPSLEEP
    bool woke = hal::woke_from_deep_sleep();

    DBG_PRINT("[POWER] Boot #%lu", boot_count);
    if (woke) {
        DBG_PRINTLN(F(" (woke from deep sleep)"));
        was_sleeping = true;
    } else {
        DBG_PRINTLN(F(" (cold boot or reset)"));
        was_sleeping = false;
    }
    #else
    DBG_PRINT("[POWER] Boot #%lu (deep sleep disabled)\n", boot_count);
    #endif
}

// ============================================================================
// Sleep management
// ============================================================================

/**
 * @brief Prepare all subsystems for deep sleep, then enter sleep
 */
static void enter_sleep() {
    DBG_PRINTLN(F("[POWER] Idle timeout reached — preparing for deep sleep"));

    // 1. Release all keys on the BLE host
    #if ENABLE_BLE_HID && IS_MASTER
    ble_hid_release_all();
    hal::delay_ms(50);  // Ensure report is sent
    #endif

    // 2. Shut down BLE
    #if ENABLE_BLE_HID && IS_MASTER
    ble_hid_shutdown();
    #endif

    // 3. Shut down ESP-NOW
    #if ENABLE_SPLIT_LINK
    split_link_shutdown();
    #endif

    // 4. Mark that we're going to sleep
    was_sleeping = true;

    // 5. Enter deep sleep with GPIO wake
    DBG_PRINT("[POWER] Sleeping... wake mask: 0x%08llX\n",
              (unsigned long long)DEEPSLEEP_WAKEUP_PIN_MASK);

    hal::enter_deep_sleep(DEEPSLEEP_WAKEUP_PIN_MASK);

    // --- Execution never reaches here ---
    // On wake, the MCU reboots and runs setup() from the beginning.
}

void power_check_sleep(uint32_t last_activity_ms) {
    #if !ENABLE_DEEPSLEEP
    return;
    #endif

    uint32_t now = hal::millis_now();
    uint32_t idle_time = now - last_activity_ms;

    if (idle_time >= DEEPSLEEP_TIMEOUT_MS) {
        enter_sleep();
    }
}

void power_force_sleep() {
    #if ENABLE_DEEPSLEEP
    enter_sleep();
    #else
    DBG_PRINTLN(F("[POWER] Deep sleep is disabled, ignoring force_sleep()"));
    #endif
}

bool power_was_wake() {
    return was_sleeping && hal::woke_from_deep_sleep();
}

uint32_t power_boot_count() {
    return boot_count;
}
