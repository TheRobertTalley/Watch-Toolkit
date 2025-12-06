# T-Watch S3 PDM microphone monitor

Minimal Arduino sketch to read the on-board PDM mic (Data IO47, Clock IO44) and print an ASCII VU meter over USB serial.

## Build and flash
From this repo root:
```sh
cd examples/twatch_s3_pdm_monitor
pio run -e t-watch-s3-pdm-monitor -t upload
pio device monitor -b 115200
```
(If PlatformIO still complains about the board ID, make sure you're in this folder so it can see `boards_dir = ../../boards` and `board_build.variants_dir = ../../variants`.)

## What you should see
- Lines like ` -34.2 dBFS |###########                     |` updating as you talk or tap the watch.
- If you see near-silence values around `-90 dBFS`, the mic is idle; speak or tap the case to raise the bar.

## Pins used (from pinout.jpeg)
- PDM mic data: IO47
- PDM mic clock: IO44

## Notes
- This uses the ESP-IDF I2S driver in PDM RX mode (I2S_NUM_0). No extra libraries are required.
- The sketch avoids touching the TFT or LoRa pins, so it is safe to run on a stock T-Watch S3.
