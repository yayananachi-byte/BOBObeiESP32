# ESP32-S3 SOSEXY BLE Remote

Target: Waveshare ESP32-S3-Touch-AMOLED-2.16 (480 x 480 AMOLED).

## Features

- Fast BLE scan with selectable device list and highlighted `SOSEXY` device.
- Swipeable home screen: the first 2 x 2 page contains Control, BLE,
  Balance Ball, and Motion; the second page contains Settings, Vibration
  Patterns, and two reserved cards.
- Both home pages use equal square grid cells, 24 px inner margins and 20 px
  gaps. All interface labels and status messages are in Chinese, using bundled
  Source Han Sans glyphs (see `main/fonts/README.md`).
- BLE GATT write to `0000ee03-0000-1000-8000-00805f9b34fb`.
- Independent 0-100 vertical controls for suction, vibration, and electric output.
- Each control uses a 200 x 268 px rectangular touch area with a bottom-up fill
  and a centered percentage; drag anywhere on the bar to change intensity.
- Manual suction, vibration, and electric values persist independently while
  moving between control pages, so all three can run at the same time.
- Horizontal swipe switches only between the three motor-control pages.
- Swipe upward from the bottom navigation bar to return to the home screen.
- QMI8658 balance-ball mode; tilt magnitude maps from 0-45 degrees to 0-100.
- QMI8658 motion mode with independent S/V/E switches: linear acceleration
  above the stillness dead zone maps to every enabled output. It sends one stop
  command when motion ends, then sends no repeated commands while still.
- Manual, Balance Ball, and Motion outputs are mixed per motor; the highest
  active request wins, and stopping one mode does not clear the other sources.
- Event-driven, fair BLE command queue with latest-value coalescing and a
  requested 7.5-15 ms connection interval.
- BOOT/GPIO0 resets the ball center. Electric output is disabled whenever the game starts.
- NVS-backed settings for ball sensitivity, ball travel/output limit, motion
  sensitivity, and display brightness.
- Live AXP2101 battery percentage and charging-state display.
- Three selectable vibration patterns: a five-second 0-100 ramp, with a
  250 ms peak and an
  immediate reset to zero; a random 20/40/60/80/100 level every five seconds;
  and a 100% pulse with one second on and one second off. Pattern outputs have
  independent suction/vibration/electric switches; only vibration is enabled
  initially, and electric is reset to off every time the pattern starts.
- Global STOP button on every page.

## Build and flash

```powershell
$env:IDF_TOOLS_PATH='D:\ESP32-IDF\.espressif\v6.1'
$env:IDF_PYTHON_ENV_PATH='D:\esp32-projects\.idf-python\idf6.1_py3.11_env'
. 'D:\ESP32-IDF\.espressif\v6.1\esp-idf\export.ps1'
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash
```

On first use, keep the board still while the game calibrates. Open Control from the
home screen, then swipe horizontally between Suction, Vibration, and Electric.
Swipe upward on the bottom HOME bar from any subpage to return to the home screen.
Open Motion, select S/V/E, and press START to enable acceleration-controlled
outputs. Manual, Motion, and Balance Ball sources can operate concurrently;
the global STOP button is the only action that clears every source.
