# ESP32 dash panel: a round gauge for SimHub

A small round dashboard for sim racing. A 2.1" round screen driven by an ESP32-S3 shows
**RPM, speed, gear and fuel**, fed live by [SimHub](https://www.simhubdash.com/) over a plain USB cable.
No Wi-Fi, no extra software on the PC besides SimHub itself.

- RPM arc with tick marks, amber then red as you climb, and a flashing red ring in the red zone
- Big speed readout in the middle, gear in a circle at the bottom (it flashes at the shift point)
- Fuel bar that turns red when the tank is low
- The rev scale and redline adapt to the car (SimHub sends them)
- Shows **NOT CONNECTED** whenever there is no data, so you always know what you're looking at

Built with ESP-IDF 5.5 and LVGL 9. It runs at about 46-50 FPS.

## Hardware

Everything is on one board, so there is nothing to wire up.

| Part | What it is |
|------|------------|
| Board | ESP32-S3 with 8 MB flash and 8 MB octal PSRAM (ESP32-S3-WROOM-1-N8R8). I used a Waveshare-style **ESP32-S3-LCD-Driver-Board**. |
| Display | **2.1" round, 480x480**, ST7701S controller, 16-bit RGB interface |
| Link to the PC | The board's native USB-C port (it shows up as a serial/COM port) |

Where I got mine (AliExpress):
- <https://fr.aliexpress.com/item/1005007490447257.html>
- <https://fr.aliexpress.com/item/1005007352210855.html>

Why this combination works well: the screen is driven over a parallel RGB bus straight from the ESP32-S3, with the
frame buffer in the 8 MB PSRAM. That is what makes a 480x480 display smooth. The same USB port is used for
flashing, logs and SimHub data, so a single cable does everything.

If you use another board or panel, the pin map and the ST7701S init sequence are in `main/lcd_panel.c`.
The pins I use:

| Function | GPIO |
|----------|------|
| RGB data B0..B4 | 5, 45, 48, 47, 21 |
| RGB data G0..G5 | 14, 13, 12, 11, 10, 9 |
| RGB data R0..R4 | 46, 3, 8, 18, 17 |
| HSYNC / VSYNC / DE / PCLK | 38 / 39 / 40 / 41 |
| Panel init (3-wire SPI): SDA / SCL / CS | 1 / 2 / 42 |
| Backlight (active low) | 6 |

The touch controller on these boards is not used.

## How it works

```
Game -> SimHub -> "Custom serial device" -> USB -> ESP32-S3 -> round screen
```

SimHub sends one short text line per update, many times a second:

```
R:5200,S:143,G:4,F:62,M:8000,L:6500
```

| Key | Meaning |
|-----|---------|
| `R` | engine rpm |
| `S` | speed, km/h |
| `G` | gear: `R`, `N` or `1`..`9` |
| `F` | fuel, 0-100 % |
| `M` | top of the rev scale (optional, 3000-12000) |
| `L` | where the redline starts (optional) |

Any key order works, a missing key keeps its last value, and junk lines are ignored (and logged).
If nothing valid arrives for 2 seconds the panel zeroes the values and shows NOT CONNECTED.

## Build and flash

One-time setup (ESP-IDF 5.5):

    git clone --depth 1 --branch v5.5.6 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.6
    ~/esp/esp-idf-v5.5.6/install.sh esp32s3
    sudo usermod -aG dialout $USER   # then log out/in (serial port access)

Then, with the board plugged in:

    . ~/esp/esp-idf-v5.5.6/export.sh
    idf.py build flash monitor -p /dev/ttyACM0

LVGL and `esp_lvgl_port` are downloaded automatically by the ESP-IDF component manager on the first build.
If the board doesn't show up as `/dev/ttyACM0`, try a different USB cable (some are charge-only), or hold
BOOT, tap RESET, release BOOT to force download mode.

## Using it with SimHub

First enable the plugin: **Add/remove features** (bottom-left of SimHub) > **Custom serial devices**. Then in the left menu:
**Custom serial devices > Add new device**

- Serial port: the panel's port (check Device Manager for the COM number). Baud rate: anything, native USB ignores it.
- Turn **DTR and RTS off** if the board reboots when SimHub connects (they can trigger the ESP32-S3 auto-reset).
- Add one **Update message**, switch the editor to **Javascript**, and use (tested in SimHub):

      return 'R:' + Math.round($prop('DataCorePlugin.GameData.NewData.Rpms') || 0)
      + ',S:' + Math.round($prop('DataCorePlugin.GameData.NewData.SpeedKmh') || 0)
      + ',G:' + ($prop('DataCorePlugin.GameData.NewData.Gear') || 'N')
      + ',F:' + Math.round($prop('DataCorePlugin.GameData.NewData.FuelPercent') || 0)
      + ',M:' + Math.round($prop('DataCorePlugin.GameData.NewData.CarSettings_MaxRPM') || 0)
      + ',L:' + Math.round($prop('DataCorePlugin.GameData.NewData.CarSettings_RedLineRPM') || 0)
      + '\n';

  (Javascript mode needs the explicit `return`, and `$prop('...')` instead of `[Property]`, which is NCalc syntax.)
  If a property name isn't found, pick the rpm max / redline ones from SimHub's property picker, or just drop `M` and `L`.
- Enable the device and start a game. NOT CONNECTED disappears and the gauge follows the car.

Want mph? Use `SpeedMph` in the formula and change the "km/h" label in `main/gauge_ui.c`.

## Trying it without SimHub

    pip install pyserial
    python3 tools/simhub_test.py /dev/ttyACM0

This sends fake telemetry so you can check the panel on its own.

## Troubleshooting

- **Stays on NOT CONNECTED**: the COM port is wrong, or something else is holding it
  (`idf.py monitor`, Arduino IDE, a script). Only one program can use the port at a time.
  Also check in SimHub that `Rpms` and `SpeedKmh` actually move while you drive.
- **The log says `ignored line`**: the panel received text it doesn't understand. The log shows the line,
  so you can see what SimHub is really sending.
- **Board reboots when SimHub connects**: turn DTR and RTS off in the device settings.
- **Wrong rev scale**: check the `M` and `L` values SimHub sends, or drop them to get the default 8000 rpm scale.

## What's where

| File | What it does |
|------|--------------|
| `main/main.c` | Starts the screen, LVGL and the SimHub task |
| `main/lcd_panel.c` | Panel pins, ST7701S init, RGB bus setup |
| `main/gauge_ui.c` | The whole gauge UI (arc, ticks, speed, gear, fuel, NOT CONNECTED) |
| `main/simhub.c` | Reads and parses the lines coming from SimHub |
| `main/vehicle_data.c/.h` | The shared state the UI draws (`vehicle_state_t`) |
| `main/simulator.c` | Fake-driving demo, not built by default (add it to `main/CMakeLists.txt` and call `simulator_start()`) |
| `tools/` | Helper scripts: test sender, logo and digit image generators |

Rev colours, shift light and redline proportions are at the top of `main/gauge_ui.c`.

## Your own logo

The splash screen and the small logo on the gauge come from `logo.svg`. To use yours:

    tools/svg_to_lvgl.sh your-logo.svg

(needs ImageMagick and Python Pillow). It regenerates `main/logo_img.c`; then rebuild and flash.
