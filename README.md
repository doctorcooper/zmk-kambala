# ZMK firmware for Kambala

The bootloader and compiled firmware can be downloaded in the [releases section](https://github.com/aroum/zmk-kambala/releases). You can change keymap using [this tool](https://nickcoutsos.github.io/keymap-editor/).

-----

## Automatic Sleep

On battery power, each half enters deep sleep after 15 minutes without key or
sensor activity, even while connected over Bluetooth. Pressing a key wakes it.
USB power prevents automatic deep sleep on the half connected to the cable.
Manual soft off remains available through the combo described below.

The shield builds an adapted copy of the fork's `activity.c` to remove its BLE
connection restriction. The dependency checkout stays unchanged. If the fork
changes the relevant code, the build stops and asks for the adaptation to be reviewed.

## LED Indication

The keyboard features 4 status LEDs (LED 1 to LED 4) to display battery charge, active Bluetooth profiles, and connection status.

### Soft Off Countdown

Hold the keys at positions 3 and 4 (R + T on the default layer) for 3 seconds,
then release to turn both halves off. The four LEDs gradually fill in sequence
on each half. Once the countdown is complete, all four pulse together to signal
that the combo can be released. Releasing early cancels shutdown and clears the
countdown. Battery, USB, and connection indications yield to the countdown.
The right half reverses the PWM channel order so the physical LED sequence
matches the left half for all indications.

### 🔋 Battery Status

*(Legend: 🟢 = Solid ON, 🟡 = Blinking, 🔵 = Blinks once ⚫ = OFF)*

When checking battery status (e.g. at startup or when triggered), the first 3 LEDs indicate the battery level:

* **0% – 15%**: LED 1 blinks 3 times ── 🟡 ⚫ ⚫ ⚫
* **16% – 30%**: LED 1 is solid ON ── 🟢 ⚫ ⚫ ⚫
* **31% – 50%**: LED 1 is solid, LED 2 blinks 3 times ── 🟢 🟡 ⚫ ⚫
* **51% – 70%**: LED 1 & 2 are solid ON ── 🟢 🟢 ⚫ ⚫
* **71% – 90%**: LED 1 & 2 are solid, LED 3 blinks 3 times ── 🟢 🟢 🟡 ⚫
* **91% – 99%**: LED 1, 2, and 3 are solid ON ── 🟢 🟢 🟢 ⚫
* **100% (Fully Charged)**: All 3 LEDs blink once together ── 🔵 🔵 🔵 ⚫(Blinking)

### 📶 BLE Profile Selection

When switching active Bluetooth profiles, the corresponding LED blinks once:

* **Profile 1**: LED 1 blinks ── 🔵 ⚫ ⚫ ⚫
* **Profile 2**: LED 2 blinks ── ⚫ 🔵 ⚫ ⚫
* **Profile 3**: LED 3 blinks ── ⚫ ⚫ 🔵 ⚫

### 🔌 USB Powered Animation

When the USB cable is plugged in, a wave animation runs across all 4 LEDs:

1. LEDs turn ON sequentially: LED 1 ➡️ LED 2 ➡️ LED 3 ➡️ LED 4 (🟢 🟢 🟢 🟢)
2. LEDs turn OFF in reverse order: LED 4 ➡️ LED 3 ➡️ LED 2 ➡️ LED 1 (⚫ ⚫ ⚫ ⚫)

### 🔗 Connection Status

#### Bluetooth Connection Status (Central/Left Half)

If the keyboard is active but disconnected from the host device (trying to connect), the LED corresponding to the active profile stays solid ON, while LED 4 blinks once every 4 seconds:

* **Profile 1 disconnected**: 🟢 ⚫ ⚫ 🟡
* **Profile 2 disconnected**: ⚫ 🟢 ⚫ 🟡
* **Profile 3 disconnected**: ⚫ ⚫ 🟢 🟡

*(Legend: 🟢 = Solid ON, 🟡 = Blinking, ⚫ = OFF)*

#### Peripheral Connection Status (Peripheral/Right Half)

If the split peripheral right half loses connection to the central left half:

* **Split disconnected**: ⚫ ⚫ ⚫ 🟡 (LED 4 blinks once every 4 seconds)
