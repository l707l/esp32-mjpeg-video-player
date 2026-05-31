# ESP32 MJPEG Video Player

Play MJPEG videos from SD card on ESP32 with TFT display.

## Project Structure

```
esp32-mjpeg-video-player/
├── esp32-video-player/    # ESP32 Arduino sketch
│   ├── esp32-video-player.ino
│   ├── MjpegClass.h
│   ├── DisplayConfig.h     # Display pin config
│   ├── SDConfig.h         # SD card config
│   └── README.md
├── tft-video-tool/         # PC video converter tool
│   ├── main.py
│   ├── widgets.py
│   ├── styles.py
│   ├── utils.py
│   ├── requirements.txt
│   ├── README.md
│   └── bin/                # Placeholder for bundled ffmpeg
└── README.md               # This file
```

## Quick Start

### 1. Convert Videos

Use the **TFT-Video-Tool** to convert your videos to MJPEG format:

```bash
cd tft-video-tool
pip install -r requirements.txt
python main.py
```

Or use CLI mode for batch conversion:
```bash
python main.py --cli --input video.mp4 --output video.mjpeg --width 240 --height 320 --fps 30
```

### 2. Prepare ESP32

1. Install Arduino libraries:
   - `Arduino_GFX_Library` by Moon On Our Nation
   - `JPEGDEC` by bitbank2

2. Configure display in `esp32-video-player/DisplayConfig.h`

3. Upload sketch to ESP32

### 3. Play Videos

Copy `.mjpeg` files to SD card. The player auto-scans all folders.

## Supported Displays

| Display | Resolution | Notes |
|---------|------------|-------|
| ILI9341 | 240x320 | Most common (ESP32-2432S028 CYD) |
| ILI9488 | 320x480 | Larger 3.5" displays |
| ST7789 | 240x320 | Some CYD variants |

## Features

- Auto-detect display type
- Dynamic buffer allocation
- Auto-scan all folders on SD card
- Button controls (next/prev video)
- Real-time FPS counter
- Support for SPI and SDMMC modes

## ESP32 Pinout

```
Display (SPI):
  SCK  → GPIO14
  MOSI → GPIO13
  MISO ← GPIO12 (optional)
  CS   → GPIO15
  DC   → GPIO2
  BL   → GPIO21

SD Card (SPI):
  CS   → GPIO5
  MOSI → GPIO23
  MISO ← GPIO19
  SCK  → GPIO18
```

## Documentation

- [ESP32 Player README](esp32-video-player/README.md)
- [TFT Video Tool README](tft-video-tool/README.md)

## License

MIT License