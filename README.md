# Round car gauge — ESP32-S3-LCD-Driver-Board + 2.1" ST7701S 480x480

ESP-IDF 5.5 + LVGL 9 (via esp_lvgl_port). Shows RPM (arc + ticks), speed, gear and fuel. Logo: `logo.svg` -> `tools/svg_to_lvgl.sh` (swap in your own SVG).
Data comes from SimHub over USB (`main/simhub.c`) into `vehicle_state_t` (see `main/vehicle_data.h`).
Without data (never connected, or nothing received for 2 s) the gauge shows NOT CONNECTED with zeroed values.
`main/simulator.c` (fake driving demo) is no longer built; add it back to `main/CMakeLists.txt` and call `simulator_start()` to use it.

## One-time setup
    git clone --depth 1 --branch v5.5.6 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.6
    ~/esp/esp-idf-v5.5.6/install.sh esp32s3
    sudo usermod -aG dialout $USER   # then log out/in (serial port access)

## Build & flash
    . ~/esp/esp-idf-v5.5.6/export.sh
    idf.py build flash monitor -p /dev/ttyACM0

## Using it with SimHub
The panel's USB port shows up as a serial port (COM port on Windows, `/dev/ttyACM0` on Linux).
First enable the plugin: **Add/remove features** (bottom-left of SimHub) > **Custom serial devices**. Then in the left menu:
**Custom serial devices > Add new device**

- Serial port: the panel's port. Baud rate: anything (native USB ignores it).
- Turn **DTR and RTS off** if the board reboots when SimHub connects (they can trigger the ESP32-S3 auto-reset).
- Add one **Update message**, switch the editor to **Javascript**, and use (tested in SimHub):

      return 'R:' + Math.round($prop('DataCorePlugin.GameData.NewData.Rpms') || 0)
      + ',S:' + Math.round($prop('DataCorePlugin.GameData.NewData.SpeedKmh') || 0)
      + ',G:' + ($prop('DataCorePlugin.GameData.NewData.Gear') || 'N')
      + ',F:' + Math.round($prop('DataCorePlugin.GameData.NewData.FuelPercent') || 0)
      + ',M:' + Math.round($prop('DataCorePlugin.GameData.NewData.CarSettings_MaxRPM') || 0)
      + ',L:' + Math.round($prop('DataCorePlugin.GameData.NewData.CarSettings_RedLineRPM') || 0)
      + '\n';

  (Javascript mode needs the explicit `return`, and `$prop('...')` instead of `[Property]`: that is NCalc syntax.)

  Protocol: one line per update, `R:<rpm>,S:<km/h>,G:<R|N|1..9>,F:<fuel %>,M:<max rpm>,L:<redline rpm>`;
  missing keys keep their last value. `M`/`L` resize the rev scale and red zone per car (0 or absent = 8000 / 81 % of max).
  If a property name isn't found, pick the rpm max/redline ones from SimHub's property picker (or just drop `M`/`L`).
- Enable the device and start a game: NOT CONNECTED disappears and the gauge follows the game (log line `SimHub connected`).

Test without SimHub: `python3 tools/simhub_test.py /dev/ttyACM0`
Close `idf.py monitor` first: only one program can hold the port.
