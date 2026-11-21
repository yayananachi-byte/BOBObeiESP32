# Actual LVGL UI preview

This Linux/WSL tool compiles `main/ui.c` directly against the same LVGL source
used by the firmware. Only hardware access is stubbed. It renders both home
pages, the manual control at 0/50/100%, and all remaining pages into 480×480
framebuffers, and reports labels outside their parent bounds. The mock BLE,
IMU and battery values do not constitute a hardware test.

From the project directory, with GCC/G++, CMake and Make installed:

```sh
cmake -S tools/ui_preview -B build/ui_preview -DCMAKE_BUILD_TYPE=Release
cmake --build build/ui_preview -j8
cd build/ui_preview
./ui_preview
```

Captures are saved in `build/ui_preview/screenshots`. Convert them to PNG and
an overview sheet with Python and Pillow from the project directory:

```sh
python tools/ui_preview/convert.py build/ui_preview/screenshots
```

The tool exits with status 2 if a visible label exceeds its parent bounds.
Screenshots also require visual inspection for overlaps and alignment.
