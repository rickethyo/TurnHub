# TurnHub Atlas ESP32

This directory contains the ESP32 port of the TurnHub Atlas controller.

The existing Python controller in `Controller/` remains the known-good reference implementation while features are ported incrementally.

## Current prototype hardware

- ESP32-WROOM-32 DevKit-style board
- Atlas master button: GPIO 32 to GND
- Button uses the ESP32 internal pull-up
- No Atlas buzzer

## Current bring-up milestone

The initial firmware intentionally does only a few things:

1. Boots the Atlas ESP32.
2. Configures GPIO 32 as the master button.
3. Starts a `TurnHub-Atlas` Wi-Fi access point on channel 6.
4. Initializes ESP-NOW on the same radio/channel.
5. Starts an HTTP server on port 80.
6. Serves a simple status page showing Atlas state, master-button state, ESP-NOW status, and a placeholder Sigil count.

The AP is open during this prototype bring-up stage. Authentication and production network behavior will be added later.

## Build and upload

This project uses PlatformIO with the Arduino ESP32 framework.

From the `Atlas` directory:

```text
pio run
pio run --target upload
pio device monitor
```

Serial monitor speed is 115200 baud.

After boot, connect a phone or computer to `TurnHub-Atlas` and browse to the IP printed in the serial monitor. The ESP32 SoftAP address will normally be `192.168.4.1`.

## Migration strategy

Port Atlas incrementally rather than rewriting the full controller at once:

1. Atlas hardware and web bring-up
2. ESP-NOW Atlas/Sigil transport
3. One Sigil end-to-end
4. Lobby and game-engine state
5. Sigil LED/audio commands
6. Web portal and seat controls
7. Persistence, profiles, and logs

The existing `Controller/` modules should be treated as behavioral references during the port.
