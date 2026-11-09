# Lysis Epub Reader

**Firmware V1.01 Beta for the M5Stack Paper S3** — an offline EPUB reader built around an e-ink display, local storage and a low-power embedded workflow.

<p align="center">
  <img src="assets/boot-screen-small.png" width="270" alt="Lysis Epub Reader boot screen">
</p>

## Overview

Lysis Epub Reader turns the M5Stack Paper S3 into a self-contained EPUB reader. The firmware manages a local library on microSD, paginates EPUB chapters, remembers the reading position, searches text locally and keeps the interface responsive on an e-ink panel.

The project is intentionally offline-first. WiFi is used only for optional NTP time synchronization. Books, reading state, bookmarks and battery history stay on the device.

## Features

### Library and reading

- Scans EPUB files from `/sd/books`.
- Persistent library index and title cache.
- Paginated EPUB chapters with configurable typography.
- Six reading sizes: **80%, 100%, 120%, 140%, 160% and 200%**.
- Adjustable line spacing and side margins.
- Page navigation by touch, chapter list, bookmarks and Go To page.
- Reading position, bookmarks and layout state persist across restarts.
- Cover image and internal JPEG/PNG images with aspect-ratio scaling.
- Image references inside SVG documents are supported.

### Search

- Whole-word search, so a query does not match inside compound words.
- Result list with chapter and text preview.
- Opening a result jumps to the matching page.
- **Back** returns to the page where the search was started.
- Chapter text cache for repeated searches.

### Interface

- E-ink-focused UI with large touch targets and high-contrast text.
- Partial refresh during normal reading.
- Full refresh on first open, after a font change and during periodic anti-ghosting.
- Status bar with battery level, page position and bookmark state.
- Settings for dark mode, images, display mode, rotation, auto power-off, NTP clock and alarm.

### Power and battery

- 3.7 V / 1800 mAh single-cell LiPo with LGS4056H charger.
- Learned charge/discharge endpoints.
- LiPo open-circuit-voltage curve for a realistic percentage slope.
- **Monotonic discharge percentage**: while the device is discharging, voltage recovery cannot make the displayed percentage increase.
- Charging/full hysteresis, low-battery cutoff and safety shutdown.
- Battery history written to `/sd/books/.state/_bat_history.csv`.
- Persistent model and last accepted percentage in `_bat_cycle.dat`.
- Power-off turns off the display, speaker, IMU, WiFi, SD and SPI peripherals. The battery-backed BM8563 RTC keeps time; the physical power button is the only wake source.

### Connectivity and utilities

- USB Disk Mode to access the microSD from a computer.
- Optional WiFi NTP synchronization.
- Local clock and alarm.
- Deep-sleep boot screen.

## Optimizations

- Binary search for page offsets.
- Cached font metrics and text widths.
- Cached chapter text for search.
- Indexed ZIP, manifest and title lookups.
- Single-pass HTML tag matching.
- Binary search in text truncation.
- Direct ZIP extraction without an intermediate copy.
- Page count limited to the actual chapter count.
- Atomic state writes and bounded image/text caches.
- 240 MHz during book/chapter loading; 80 MHz baseline while reading.

## Hardware

| Item | Value |
|---|---|
| MCU | ESP32-S3R8, dual-core Xtensa LX7, 240 MHz |
| Memory | 8 MB PSRAM, 16 MB flash |
| Display | 4.7-inch e-ink, 960 × 540 |
| Touch | GT911 capacitive |
| Storage | microSD, FAT32 |
| RTC | BM8563 |
| IMU | BMI270 |
| Battery | 3.7 V, 1800 mAh LiPo |
| Charger | LGS4056H |
| Power path | PMS150G latch |
| Power button | short press on, double press off |

## Repository layout

```
.
├── src/
│   ├── app.c              application, UI, reader and search
│   ├── bridge.cpp         M5Unified/M5GFX hardware bridge
│   ├── epub.cpp           EPUB/OPF/NCX and ZIP access
│   ├── htmltext.c         HTML to text conversion
│   ├── paginate.c         word wrapping and page layout
│   ├── battery.c          battery curve and history
│   ├── state.c            settings and reading state
│   └── ...
├── lib/                   miniz and tinyxml2
├── boards/                PlatformIO board definition
├── tools/merge_firmware.py
├── assets/                boot screen images
├── firmware/              prebuilt merged firmware
├── platformio.ini
├── partitions.csv
├── MANUAL.md
└── CHANGELOG.md
```

## Build and flash

```bash
git clone https://github.com/raiko-sama/LysisEpubReader.git
cd LysisEpubReader
pio run -e m5stack-papers3
pio run -e m5stack-papers3 -t upload --upload-port COM3
```

The complete step-by-step guide, including prerequisites, microSD preparation, USB Disk Mode, battery history and troubleshooting, is in **[MANUAL.md](MANUAL.md)**.

## SD card layout

```text
/
└── books/
    ├── book-one.epub
    └── book-two.epub
```

The `.state/` directory is created automatically. Do not remove it while the device is running.

## Version

**V1.01 Beta** — 2026-11-08

## License

MIT. See [LICENSE](LICENSE).
