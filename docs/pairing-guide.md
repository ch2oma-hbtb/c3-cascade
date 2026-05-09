# C3-Cascade Pairing Guide

The C3-Cascade keyboard has **two** independent pairing systems that operate separately:

1. **BLE PC Pairing** — connecting the master half to your computer
2. **Split Link Pairing** — connecting the two keyboard halves to each other

All pairing shortcuts require **holding both keys continuously for 5 seconds**. Releasing either key resets the timer.

---

## 1. Pair the Keyboard with a Computer (BLE PC Pairing)

This makes the keyboard discoverable by a new host device (phone, PC, etc.).

**Keys:** Hold **ESC + SPACE** (left half) for 5 seconds

| Step | Action |
|------|--------|
| 1 | Put your host device (phone/PC) into Bluetooth scanning mode |
| 2 | On the keyboard's **left (master) half**, press and hold **ESC** and **SPACE** together |
| 3 | Keep both keys held for 5 seconds |
| 4 | The keyboard disconnects from all previously paired hosts, clears the bond table, and begins advertising as **"C3-Cascade"** |
| 5 | On your host device, select **"C3-Cascade"** from the Bluetooth scan results |

**What happens internally:**
- All active BLE connections are disconnected
- All stored bond data is deleted (`NimBLEDevice::deleteAllBonds()`)
- BLE advertising restarts so new hosts can discover the keyboard
- Pairing uses **Just Works** (no passkey required on the keyboard)

**Pairing mode behavior:**
- While in BLE PC pairing mode, **all keys are suppressed except ESC, SHIFT, and SPACE** — the keys needed for pairing shortcuts. This prevents accidental keypresses from being sent to the host.
- The onboard LED blinks during pairing mode (Pico W only)
- Pairing mode exits automatically when:
  - A host connects via BLE, **or**
  - 60 seconds elapse (timeout)
- Deep sleep is inhibited during pairing mode

> **Note:** This fully wipes all previous host pairings. You will need to re-pair every host device afterward.

---

## 2. Re-pair the Two Keyboard Halves (Split Link)

Use this when the two halves have lost their wireless connection or you want to pair them fresh.

### On the Master (Left) Half

**Keys:** Hold **ESC + LSHIFT** for 5 seconds

| Step | Action |
|------|--------|
| 1 | On the **left (master) half**, press and hold **ESC** and **Left Shift** together |
| 2 | Keep both keys held for 5 seconds |
| 3 | The master clears all known slave devices and re-enters discovery mode, accepting new slave connections |

**Slave search mode behavior:**
- While searching for a slave, **all keys are suppressed except ESC, SHIFT, and SPACE** — the same as BLE PC pairing mode
- The onboard LED blinks during search mode (Pico W only)
- Search mode exits automatically when:
  - A slave connects and is discovered, **or**
  - 60 seconds elapse (timeout)

**Key positions on the left half:**

```
         COL0    COL1    COL2    COL3    COL4    COL5
ROW0    ESC     1       2       3       4       5
ROW1    TAB     Q       W       E       R       T
ROW2    CAPS    A       S       D       F       G
ROW3    LSHIFT  ---     Z       X       C       V
ROW4    LCTRL   LGUI    LALT    ---     SPACE   ---
```

### On the Slave (Right) Half

**Keys:** Hold **7 + SPACE** for 5 seconds

| Step | Action |
|------|--------|
| 1 | On the **right (slave) half**, press and hold **7** and **Space** together |
| 2 | Keep both keys held for 5 seconds |
| 3 | The slave clears its stored master address and re-enters discovery mode, searching for a master |

**Key positions on the right half:**

```
         COL0    COL1    COL2    COL3    COL4    COL5    COL6    COL7
ROW0    7       8       9       0       -       =       ---     BACKSPACE
ROW1    Y       U       I       O       P       [       ]       \
ROW2    H       J       K       L       ;       '       ---     Enter
ROW3    N       M       ,       .       /       Up      ---     RShift
ROW4    Space   ---     ---     FN      Left    Down    ---     Right
```

### Full Re-pair Procedure

To completely re-pair the two halves:

1. Reset the **master** half: hold **ESC + LSHIFT** for 5 seconds
2. Reset the **slave** half: hold **7 + SPACE** for 5 seconds
3. The halves will auto-discover each other and connect

> **The slave never gives up.** If no master is found, the slave stays in pairing mode and keeps scanning. You can power on the halves in any order — the slave will automatically connect once the master is available.

---

## Platform Compatibility

| Platform | BLE PC Pairing | Split Link | Notes |
|----------|---------------|------------|-------|
| ESP32-C3/C6 | Supported | **Dual Mode** | Supports both ESP-NOW and WiFi UDP slaves simultaneously |
| nRF52840 | Not supported | Supported | BLE pairing mode is a no-op |
| RP2040 (Pico 2W) | Not supported | WiFi UDP | BLE pairing mode is a no-op |

---

## Troubleshooting

- **Keys must be held continuously.** If you release either key, the 5-second timer resets. You must hold both keys for the full duration.
- **LED feedback during pairing.** On the Pico W, the onboard LED indicates status:
  - **Blinking** = searching for a connection (master in pairing mode, or slave searching for master)
  - **Solid on** = paired and connected (slave only — confirms successful connection to master)
  - **Off** = idle / normal operation (master: not in pairing mode; slave: N/A, solid on when connected)

  On boards without a simple onboard LED (XIAO ESP32-C3/C6, nRF52840), there is no LED feedback — use serial debug output for confirmation.
- **Pairing with a new host wipes all existing bonds.** After entering BLE PC pairing mode, all previously paired devices are forgotten — you'll need to re-pair each one.
- **Slave always stays in pairing mode until it finds a master.** If the master isn't powered on yet, the slave keeps scanning (ESP-NOW: broadcasts every 500ms; WiFi UDP: scans for the AP every 5 seconds). Power the halves on in any order.
- **Keys are suppressed during pairing mode.** When the master is in BLE PC pairing or slave search mode, only ESC, SHIFT, and SPACE produce keypresses. All other keys are blocked to prevent accidental input. This is also why deep sleep is inhibited during pairing mode.
- **After waking from sleep**, the keyboard automatically tries to reconnect. If reconnection fails within 5 seconds (`BLE_RECONNECT_TIMEOUT`), advertising restarts automatically.