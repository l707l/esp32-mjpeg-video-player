# TFT Video Tool

Convert videos to MJPEG format for ESP32 TFT displays.

## Features

- **Preview** video before conversion
- **Multiple display presets** including 240x320 portrait (for 2.8" ILI9341)
- **Batch mode** - convert all videos in a folder at once
- **CLI mode** - headless conversion for automation
- **Quality presets** - High/Medium/Low with customizable settings
- **Dark/Light theme** support
- **Progress bar** for conversion progress

## Installation

### Prerequisites

- Python 3.8+
- FFmpeg (required for conversion)

### Install Dependencies

```bash
pip install -r requirements.txt
```

### Install FFmpeg

**Windows:**
```powershell
winget install ffmpeg
# or download from https://ffmpeg.org/download.html
```

**macOS:**
```bash
brew install ffmpeg
```

**Linux:**
```bash
sudo apt install ffmpeg   # Ubuntu/Debian
sudo dnf install ffmpeg   # Fedora
sudo pacman -S ffmpeg     # Arch
```

## Usage

### GUI Mode

```bash
python main.py
```

1. Click **📂** to open a video file
2. Select a **preset** or enter custom dimensions
3. Adjust FPS, quality, and codec settings
4. Click **EXPORT** to convert

### CLI Mode (Headless)

Convert a single video:
```bash
python main.py --cli \
    --input video.mp4 \
    --output video.mjpeg \
    --width 240 \
    --height 320 \
    --fps 30 \
    --quality 10
```

**Full CLI options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--cli` | Enable CLI mode | - |
| `--input` | Input video file | (required) |
| `--output` | Output video file | (auto-generated) |
| `--width` | Output width | 240 |
| `--height` | Output height | 320 |
| `--fps` | Frames per second | 30 |
| `--quality` | MJPEG quality (1-31) | 10 |
| `--vcodec` | Video codec (mjpeg/cinepak) | mjpeg |
| `--acodec` | Audio codec (mp3/pcm/none) | mp3 |
| `--audio-rate` | Audio sample rate (Hz) | 44100 |
| `--aspect` | Aspect mode (fit/cover/stretch) | fit |
| `--suffix` | Output file suffix | .mjpeg |

### Batch Mode

Click **📁** to select a folder. All videos in the folder will be converted with the same settings.

## Display Presets

| Preset | Resolution | Notes |
|--------|-----------|-------|
| 320x170 (Landscape) | 320x170 | Standard wide |
| 280x240 (Square) | 280x240 | Square format |
| 170x320 (Portrait) | 170x320 | Tall portrait |
| **240x320 (Portrait 2.8")** | **240x320** | **Most common for ESP32-2432S028** |
| 320x480 (Landscape 3.5") | 320x480 | Larger 3.5" displays |
| 240x280 (Portrait Small) | 240x280 | Smaller portrait |

## Recommended Settings

### For ESP32-2432S028 (ILI9341 240x320)

```
Width: 240
Height: 320
FPS: 24-30
Quality: 8-12
Codec: mjpeg
```

### For ILI9488 320x480

```
Width: 320
Height: 480
FPS: 24
Quality: 8-10
Codec: mjpeg
```

## Troubleshooting

### "FFmpeg not found"

1. Install FFmpeg
2. Add FFmpeg to PATH
3. Or place `ffmpeg.exe` in the `bin/` folder next to `main.py`

### Video plays but has artifacts

- Lower the quality value (try Q=5 or Q=2)
- Reduce FPS to 24 or 15

### Audio out of sync

- Use audio codec "mp3"
- Ensure audio rate is 44100 Hz

## License

MIT License