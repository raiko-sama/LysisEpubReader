# Lysis Epub Reader

**Firmware V1.0.2 for the M5Stack Paper S3** — an offline EPUB reader built around an e-ink display, local storage and a low-power embedded workflow.

<p align="center">
  <img src="assets/version_splash.png" width="270" alt="Lysis Epub Reader boot screen with V1.0.2">
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

### Rolling reader window

- Up to **4 chapters** cached in memory (text + layout, 3 MB text budget).
- Page-based window: **8 pages per side** in normal reading, **12 pages** in the direction of a fast flip.
- Chapter boundaries are a cache pointer switch: no "Opening chapter" interruption during normal reading.
- Conditional neighbour preload: a neighbour is loaded only when the current page is within 8 pages of a boundary (or the chapter is shorter than 8 pages), so opening a book stays fast.
- Page turns never extract or lay out text in the tap path. If the neighbouring chapter is still cold, the turn stays pending and is completed asynchronously by the I/O worker.
- Eviction is based on page distance from the open page, not plain LRU.
- **Directional boost**: after **6 consecutive flips** in the same direction (gap under 3 s) the CPU rises to 240 MHz and the window shifts forward/backward. After 3 s of idle the CPU returns to 80 MHz, the streak resets and the window becomes symmetric again.

### Search

- Whole-word search, so a query does not match inside compound words.
- Result list with chapter and text preview.
- Opening a result jumps to the matching page.
- **Back** returns to the page where the search was started.
- Chapter text cache for repeated searches.
- Search and background page counting run on the **core-0 I/O worker**, so the UI stays responsive; both use race-to-idle at 240 MHz.

### Interface

- E-ink-focused UI with large touch targets and high-contrast text.
- Partial refresh during normal reading.
- Full refresh on first open, after a font change and during periodic anti-ghosting.
- Library page scrolling uses a partial regional refresh (~0.6 s instead of a full quality refresh).
- Status bar with battery level, page position and bookmark state.
- Settings for dark mode, images, display mode, rotation, auto power-off, NTP clock and alarm.
- Firmware version shown at the bottom centre of the splash and power-off screens.

### Power and battery

- 3.7 V / 1800 mAh single-cell LiPo with LGS4056H charger.
- Learned charge/discharge endpoints.
- LiPo open-circuit-voltage curve for a realistic percentage slope.
- **Monotonic discharge percentage**: while the device is discharging, voltage recovery cannot make the displayed percentage increase.
- Charging/full hysteresis, low-battery cutoff and safety shutdown.
- Battery history written to `/sd/books/.state/_bat_history.csv`.
- Persistent model and last accepted percentage in `_bat_cycle.dat`.
- **80 MHz is the system baseline**: UI, touch and EPD timing rely on it. 240 MHz is used only for loading, layout, search and the six-flip reading boost; requests below 80 MHz are refused at the bridge level.
- Book-state writes are deferred and flushed while idle, outside the tap and render path.
- Power-off turns off the display, speaker, IMU, WiFi, SD and SPI peripherals. The battery-backed BM8563 RTC keeps time; the physical power button is the only wake source.

### Connectivity and utilities

- USB Disk Mode to access the microSD from a computer.
- Optional WiFi NTP synchronization with fast reconnect (cached BSSID/channel, scan fallback).
- Local clock and alarm.
- Deep-sleep boot screen.

## Measured performance

Values measured on the M5Stack Paper S3 with the reference EPUB library, using the on-device `@PERF` telemetry and an A/B protocol for search.

| Scenario | V1.0.2 result |
|---|---|
| Open book | **0.80–0.87 s** (no eager neighbour preload) |
| Page turn | **234–470 ms UI** (panel refresh ~1.2 s asynchronous) |
| Menu open | **193 ms** |
| Library page scroll | **627 ms** median (was ~3.3 s with full quality refresh) |
| Whole-book search | **1.43 s** at 240 MHz; A/B **1.83 s → 1.43 s (−22%)** |
| Chapter boundary while reading | no busy screen; cache switch or pending turn |
| Deferred state write | **296–750 ms** outside the tap |
| CPU policy | 80 MHz baseline; 240 MHz only for load/layout/search and after 6 fast flips |

## Optimizations

- Rolling reader window with page-distance eviction and pending boundary turns.
- Core-0 I/O worker for search, page counting, prefetch and state flush.
- Deferred book-state writes: the ~0.45 s SD write leaves the tap path.
- Single-pass library scan, metadata only; no second directory walk at boot.
- Decoded cover cache: returning to the cover does not re-extract or re-decode it.
- Partial library refresh: list band + status bar, with periodic quality cleanup.
- Menu overlay rendered with regional `epd_fast` refresh.
- Idle anti-ghosting cleanup after repeated partial refreshes.
- Batched ASCII text drawing and O(log n) accent lookup.
- Single-copy image pipeline: decode straight into the caller buffer and extract images from the ZIP directly into PSRAM.
- 2-way hashed text-width cache, `bsearch` block-tag table, 2048-entry pagination width cache.
- Non-blocking `brd_log` with a synchronous variant for protocol responses.
- Release builds compile out chatty bring-up logs and the serial test CLI.
- Binary search for page offsets, cached font metrics, indexed ZIP/manifest/title lookups, single-pass HTML tag matching.
- Atomic state writes and bounded image/text caches.

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

```text
.
├── src/
│   ├── app.c              application, UI, reader window and search
│   ├── bridge.cpp         M5Unified/M5GFX hardware bridge
│   ├── epub.cpp           EPUB/OPF/NCX and ZIP access
│   ├── htmltext.c         HTML to text conversion
│   ├── paginate.c         word wrapping and page layout
│   ├── battery.c          battery curve and history
│   ├── state.c            settings and reading state
│   └── ...
├── lib/                   miniz, stb_image and tinyxml2
├── boards/                PlatformIO board definition
├── host/                  headless GUI emulator for host-side tests
├── tests/                 host smoke test script
├── tools/                 firmware merge and performance scripts
├── assets/                boot/version screen images
├── firmware/              prebuilt merged V1.0.2 image
├── platformio.ini
├── partitions.csv
├── MANUAL.md
└── CHANGELOG.md
```

## Build and flash

Distribution build (no serial test CLI):

```bash
git clone https://github.com/raiko-sama/LysisEpubReader.git
cd LysisEpubReader
pio run -e release
pio run -e release -t upload --upload-port COM3
```

Development build with the serial test CLI (`@TAP`, `@PERF`, `@WIN`, `@SCREEN`):

```bash
pio run -e m5stack-papers3
pio run -e m5stack-papers3 -t upload --upload-port COM3
```

Artifacts:

- `.pio/build/release/firmware.bin` — application image
- `lysis-reader-1.0.2.bin` — merged 8 MB image for a complete flash
- `lysis-reader-1.0.2-dev.bin` — merged development image

The complete step-by-step guide, including prerequisites, microSD preparation, USB Disk Mode, battery history and troubleshooting, is in **[MANUAL.md](MANUAL.md)**.

## Host smoke test

```bash
pwsh host/build_host.ps1
./host/host_gui.exe tests/host_smoke.txt
```

The emulator renders the scripted screens off-screen and ends with `DONE frames=N`.

## SD card layout

```text
/
└── books/
    ├── book-one.epub
    └── book-two.epub
```

The `.state/` directory is created automatically. Do not remove it while the device is running.

## Version

**V1.0.2** — 2026-11-27

## License

MIT. See [LICENSE](LICENSE).
