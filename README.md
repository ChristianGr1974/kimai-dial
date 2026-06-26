# Kimai Dial

Firmware for an [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) that acts as a physical remote control for a self-hosted [Kimai](https://www.kimai.org/) time-tracking instance: pick a project and activity with the rotary encoder, start/stop time tracking, and see the elapsed time on the round display.

## Features

- Rotary-encoder-driven UI (vertical scrolling carousel, no touch needed)
- Browse Kimai projects/activities, start/stop timesheets, resume an already-running timesheet after reboot
- Project/activity colors from Kimai are used as the screen background
- Zero-config secrets in the source code: WiFi, Kimai server URL, username and API token are all set up **on the device itself**, via a built-in WiFi access point + captive-portal web page (no `config.h`, no recompiling to change credentials)
- "Forget WiFi" / full factory reset from the web settings page
- German/English UI (device display and web settings page), switchable from the Settings menu
- Timezone (with correct DST rule) selectable from the web settings page - no recompiling needed

## Hardware

- M5Stack M5Dial (ESP32-S3, no PSRAM)
- USB-C cable for flashing

## First-time setup

1. Flash the firmware (see below).
2. On first boot (no WiFi credentials stored yet), the device starts its own access point: **`KimaiDial-Setup`** / password **`kimaidial`**. A QR code on the display lets you join it directly from your phone.
3. After connecting, your phone should show a "Sign in to network" prompt (captive portal) that opens the setup page automatically. If not, open `http://192.168.4.1/` manually.
4. Enter your WiFi SSID/password and save - the device reboots and joins your network.
5. Open Settings → Setup on the device again (now shows a QR code for the device's LAN IP), or go to Settings → Status to read the IP directly. Open that URL in a browser on the same network and fill in your Kimai server URL, username, and API token.

Everything is stored in the ESP32's NVS flash (not in the source code), and can be changed again any time the same way. "Factory Reset" on the web page clears everything and returns the device to the AP setup flow.

## Building and flashing

This project uses [PlatformIO](https://platformio.org/).

```bash
pio run                                    # build
pio run -t upload                          # flash firmware
pio run -t uploadfs                        # upload the UI fonts (data/*.vlw) - needed once, and again whenever fonts change
```

`platformio.ini` has `upload_port`/`monitor_port` set to a specific path (`/dev/cu.usbmodem1101`); override it for your machine, e.g.:

```bash
pio run -t upload --upload-port /dev/tty.YOUR_PORT
```

## Project structure

The code is organized into three pragmatic layers (see comments in the source for the full reasoning):

- **Domain** (`app_state.h`): plain data types (`AppState`, `KimaiProject`, `KimaiActivity`, `AppContext`)
- **Application** (`state_handlers.h/.cpp`): the state machine / use-cases tying domain and infrastructure together
- **Infrastructure** (`kimai_client`, `settings_store`, `wifi_manager`, `setup_webserver`, `carousel`, `ui_screens`, `i18n`): HTTP, NVS persistence, WiFi, the setup web server, and rendering

`KimaiClient` depends on an `ICredentialsProvider` interface (implemented by `SettingsStore`) rather than reaching into settings storage directly - the one place in the project where an interface is introduced on purpose. Elsewhere, the code intentionally avoids virtual dispatch, `std::function`, and heap allocation in hot paths (the encoder/render loop runs every ~20ms on a chip with no PSRAM).

## License

MIT, see [LICENSE](LICENSE).
