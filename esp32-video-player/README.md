# ESP32 MJPEG Video Player

Play MJPEG videos directly from SD card on your ESP32 with a TFT display.

## Supported Displays

- **ILI9341** 240x320 (2.8") - Most common, used in ESP32-2432S028 (Cheap Yellow Display)
- **ILI9488** 320x480 (3.5") - Larger TFT displays
- **ST7789** 240x320 (2.8"-3.2") - Some CYD variants

## Supported ESP32 Boards

- ESP32 Dev Module
- ESP32-2432S028 (CYD)
- Any ESP32 with SPI display

## Features

- Auto-detect display type
- Dynamic buffer allocation based on available heap
- Auto-scan for MJPEG files across all folders on SD card
- Button controls (BOOT to skip, optional PREV button)
- Real-time FPS counter and memory stats
- Support for both SPI and SDMMC SD card modes
- Graceful handling of corrupt files

## Wiring Diagram

### ILI9341 Display (240x320)

```
ESP32         ILI9341
--------      -------
3.3V   -----> VCC
GND    -----> GND
GPIO14 -----> SCK
GPIO13 -----> MOSI
GPIO12 <----- MISO (optional)
GPIO15 -----> CS
GPIO2  -----> DC
GPIO21 -----> BL (Backlight)
```

### SD Card (SPI mode)

```
ESP32         SD Card Module
--------      --------------
GPIO5  -----> CS
GPIO19 <----- MISO
GPIO23 -----> MOSI
GPIO18 -----> SCK
3.3V   -----> VCC
GND    -----> GND
```

## Installation

1. **Install required libraries** in Arduino IDE:
   - `Arduino_GFX_Library` by Moon On Our Nation
   - `JPEGDEC` by bitbank2

2. **Configure display** in `DisplayConfig.h`:
   - Uncomment your display type
   - Adjust pin numbers if needed

3. **Configure SD card** in `SDConfig.h`:
   - Choose SPI or SDMMC mode
   - Adjust pins if needed

4. **Select board** in Arduino IDE:
   - Board: `ESP32 Dev Module`

5. **Upload** the sketch to your ESP32

## SD Card Structure

Place `.mjpeg` or `.avi` files anywhere on the SD card. The player will automatically scan all folders.

```
SD Card/
├── mjpeg/
│   ├── video1.mjpeg
│   └── video2.mjpeg
├── movies/
│   └── clip.avi
└── video.mjpeg
```

## Button Controls

| Button | Action |
|--------|--------|
| BOOT (GPIO0) | Skip to next video |
| PREV (optional) | Go to previous video |

## Serial Monitor Output

Press `BOOT` button to see stats, or the player prints them at the end of each video:

```
========== PLAYBACK STATS ==========
Total frames: 300
Time used: 10000 ms
Average FPS: 30.0
Video size: 320x240 (scale: 1/2)
Read:   200 ms (5.0%)
Decode: 500 ms (12.5%)
Show:   3280 ms (82.5%)
Free heap: 180000 bytes
Min free heap: 150000 bytes
====================================
```

## Troubleshooting

### Display shows nothing
- Check wiring (especially MISO/MOSI)
- Try reducing SPI speed: set `DISPLAY_SPI_SPEED 40000000L`
- Some displays need `DISPLAY_ROTATION 2`

### Videos won't play
- Make sure files are `.mjpeg` format (not just renamed MP4)
- Use the TFT-Video-Tool to convert videos
- Check that SD card is formatted as FAT32

### Slow performance
- Lower resolution in video converter
- Reduce FPS to 24 or 15
- Increase MJPEG quality (lower Q value)

## Video Conversion

Use the **TFT-Video-Tool** to convert videos for the player:

```bash
cd tft-video-tool
python3 main.py
```

Or use CLI mode:
```bash
python3 main.py --input video.mp4 --output video.mjpeg --width 240 --height 320 --fps 30
```

## License

MIT License - See original project for credits