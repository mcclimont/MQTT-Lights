# MQTT Lights Project Notes

## Project Purpose

The MQTT-Lights project will provide four relays controllable using PicoMQTT.

## Hardware and Configuration

This project uses a single ESP32-S2 board:

### Board

#### ESP32-C3

The prefered board to use for this project.

- Board model: Seeed Studio Xiao ESP32-C3

References:

- Pinouts: https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started

#### ESP32-S2

The main issue with this board is the wireless connection can become unstable unless the gain is reduced.

- Board model: ESP32-S2 Mini (Purple board)

References:

- Pinouts: https://www.studiopieters.nl/s2-mini-pinout/

### Relay Module

ELEGOO 4 Channel DC 5V Relay Module with Optocoupler

## Device MQTT Topics

The target device publishes the following MQTT topics:

- home/relay1/set
- home/relay1/state
- home/relay2/set
- home/relay2/state
- home/relay3/set
- home/relay3/state
- home/relay4/set
- home/relay4/state

The `set` topics accept these payload values:

- ON
- OFF
- TOGGLE

The `state` topics will retrieve the current state of `ON` or `OFF`.

## Per-Light Auto-Off Timers

Each relay supports an independent countdown timer. The MQTT-Lights board owns the countdown and turns the relay off when it expires, so the timer does not depend on the screen remaining connected.

For each relay, the controller uses these topics:

- `home/relayN/timer/set`
- `home/relayN/timer/state`

Replace `N` with the relay number from 1 to 4. The `timer/set` topic accepts a positive integer duration in seconds or `CANCEL`. The Screen currently offers 1, 2, 4, and 8 hour presets, sent as `3600`, `7200`, `14400`, and `28800` seconds; other MQTT clients may send any positive integer duration. Setting a positive duration turns the light on and starts or replaces its countdown. `CANCEL` clears the countdown without changing the current light state. Sending `OFF` to the existing relay `set` topic also clears that relay's countdown; `ON` does not change an active countdown.

The `timer/state` topic publishes the remaining whole seconds, with `0` meaning no active timer. A `GET` request on this topic returns the current value. Publish an updated value when a timer is started, canceled, or expires; the screen can decrement the displayed value locally between updates.

The firmware accumulates elapsed milliseconds independently for each relay, using unsigned `millis()` deltas so counter rollover is handled. Timer checks continue in the main loop while Wi-Fi reconnects are attempted periodically without a blocking wait. Timers are in-memory only, are cleared if the MQTT-Lights controller restarts, and do not require NTP or wall-clock time.

The MQTT-Lights firmware implements this timer contract in `src/main.cpp`. Build and deploy the firmware to the controller for end-to-end operation with the Screen. Timers are held in memory and are cleared on controller restart.

## Build and Deployment

### Development Environment

The project is developed on Windows using Visual Studio Code and the pioarduino PlatformIO IDE extension. PlatformIO builds the Arduino framework firmware using the ESP32 platform and libraries declared in `platformio.ini`.

### Configure, Build, and Upload

1. Open the MQTT-Lights project folder in Visual Studio Code and allow PlatformIO to install the project dependencies.
2. In the project root, copy `include/config.example.h` to `include/config.h` and set the Wi-Fi and MQTT connection values. Keep `include/config.h` private; it is excluded from Git.
3. Connect the selected ESP32 board over USB. The default environment is the Seeed XIAO ESP32-C3. Build it with:

	```powershell
	pio run -e seeed_xiao_esp32c3
	```

	To build for the Lolin S2 Mini instead, use `pio run -e lolin_s2_mini`.
4. Upload the selected environment to the connected board:

	```powershell
	pio run -e seeed_xiao_esp32c3 -t upload
	```

	For the Lolin S2 Mini, use `pio run -e lolin_s2_mini -t upload`.

The same build and upload targets are available from the PlatformIO controls in Visual Studio Code. If PlatformIO does not detect the board's serial port, select or configure the upload port before uploading.

## Responsible AI

AI tools were used to assist with developing and documenting this project. Project maintainers are responsible for reviewing, testing, and deploying changes.
