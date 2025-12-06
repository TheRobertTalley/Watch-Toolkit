# MicroPython demo (LilyGo T-Watch S3)

This repo now includes a MicroPython sketch that matches the working pins already defined for the `t-watch-s3` variant.

## Quick start
- Flash a recent **ESP32-S3 + PSRAM** MicroPython firmware build to the watch. Example:
  - `esptool.py --chip esp32s3 -p COM22 erase_flash`
  - `esptool.py --chip esp32s3 -p COM22 --baud 921600 write_flash -z 0x0 firmware.bin`
- Install the display driver once:
  - `mpremote mip install "github:russhughes/st7789py_mpy"`
- Copy and run the demo:
  - `mpremote fs cp micropython/twatch_s3_demo.py :main.py`
  - `mpremote reset`

The watch should light the backlight, show a title card, and paint coloured blocks where you touch the screen. The background shifts every few seconds.

## Pin map used by the demo
- ST7789 (SPI3): `CS=12`, `DC=38`, `SCK=18`, `MOSI=13`, `BL=45`
- Touch (I2C1, FT6x36 @ 0x38): `SDA=39`, `SCL=40`, `INT=16`
- Display rotation: `ROTATION = 2` to match the Meshtastic/LovyanGFX setup in `variants/t-watch-s3/variant.h`

## Tweaks
- Adjust brightness by editing `BACKLIGHT_PERCENT` in `micropython/twatch_s3_demo.py`.
- If touch feels mirrored/rotated, change the `ROTATION` constant and the `map_touch` helper will keep coordinates aligned.
