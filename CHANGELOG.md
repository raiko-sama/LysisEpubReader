# Changelog

## V1.0.2 — 2026-11-27

Performance batch from the static analysis, with no UI or behaviour change.

### Reading

- Rolling reader window: up to four cached chapters (text + layout, 3 MB
  text budget), page-distance eviction and conditional neighbour preload.
- Page-based window: eight pages per side in normal reading, twelve pages
  in the direction of a fast flip.
- Chapter boundaries are a cache switch: "Opening chapter" no longer
  appears during normal reading. A cold boundary turn stays pending and is
  completed asynchronously by the worker, never extracting in the tap path.
- Directional boost: after six consecutive flips in the same direction
  (gap under 3 s) the CPU rises to 240 MHz and the window shifts; after
  three seconds of idle it returns to 80 MHz, the streak resets and the
  window becomes symmetric.
- Open book without eager preload: 0.80–0.87 s.
- Prefetch robustness: a failed neighbour is retried after a 30 s cooldown
  instead of being blacklisted for the session, and a successful commit
  clears the blacklist.

### Core-0 I/O worker

- Search, background page counting, neighbour prefetch and book-state
  flush run on a dedicated core-0 worker; the UI task stays responsive.
- Search keeps the 240 MHz race-to-idle boost until the worker completes
  (A/B on the reference book: 1.83 s → 1.43 s, −22%).
- A count job reuses the window layout when the chapter was already
  paginated, avoiding a second layout pass.

### Interface and library

- Partial library refresh: list band + status bar scroll in ~627 ms median
  instead of a full quality refresh (~3.3 s).
- Menu overlay with regional `epd_fast` refresh (193 ms open).
- Deferred book-state writes for open, bookmark and page count: the
  ~0.45 s SD write leaves the tap path (296–750 ms while idle).
- Single-pass library scan, metadata only, with no per-book state-file
  open at boot.
- Decoded cover cache: returning to the cover page does not re-extract or
  re-decode the image.
- Version shown at the bottom centre of the splash and power-off screens.

### Platform and code

- Separate `release` environment without the serial test CLI.
- Merged release artifact `lysis-reader-1.0.2.bin`; development artifact
  `lysis-reader-1.0.2-dev.bin` keeps `@TAP`/`@PERF`/`@WIN`.
- CPU guard: `brd_cpu_freq` refuses requests below 80 MHz (40 MHz is not
  admissible; 80 MHz is the system baseline for UI, touch and EPD).
- Single-copy image pipeline: decode straight into the caller buffer and
  extract images from the ZIP directly into PSRAM.
- WiFi fast reconnect using cached BSSID/channel with scan fallback.
- Batched ASCII text drawing and O(log n) accent lookup.
- 2-way hashed text-width cache, `bsearch` block-tag table, 2048-entry
  pagination width cache, non-blocking `brd_log` with a synchronous
  variant for protocol responses.
- Chatty bring-up logs compiled out in release builds.
- Dead code removal: radio module, unused library index, WiFi credential
  table and unused bridge/util/epub/battery APIs.
- Host GUI emulator and `tests/host_smoke.txt` added to the repository.

### Measured results

| Scenario | V1.0.2 |
|---|---|
| Open book | 0.80–0.87 s |
| Page turn | 1.11–1.42 s (median ~1.23–1.24 s) |
| Menu open | 193 ms |
| Library page scroll | 627 ms median |
| Whole-book search | 1.43 s (−22% vs 80 MHz A/B) |
| Deferred state write | 296–750 ms outside the tap |
| Chapter boundaries | no busy screen in normal reading |

## V1.0.1 — 2026-11-09

### Battery and power

- Reworked battery estimation with a LiPo OCV curve, learned endpoints and
  monotonic discharge tracking; the displayed percentage can no longer
  rise because of voltage recovery while discharging.
- Added persistent last accepted percentage and battery history
  (`.state/_bat_history.csv`).
- Reinforced power-off: display, speaker, IMU, WiFi, SD and SPI are
  stopped, no timer wake source is enabled, and only the power button can
  restart the device.
- RTC/clock is synchronized before power-off.
- README and MANUAL rewritten in English for the V1.0.1 release.

### Performance

- Menu overlay rendered above the frame with regional `epd_fast` refresh:
  menu open **3.35 s → 735–794 ms**.
- Deferred book-state writes: `reader_progress` marks the state dirty and
  the flush runs after 2.5 s of idle or on exit/sleep. Page turn
  **1.77–1.83 s → 1.28–1.41 s**, with the state write out of the tap path.
- Background page counting pauses during touch input, so counting never
  competes with a page turn.
- Periodic quality refresh in idle after repeated partial updates, with no
  refresh during the tap.

## V1.0.0 — 2026-11-08

- EPUB reader for M5Stack Paper S3.
- Library, pagination, chapters, bookmarks and whole-word search.
- Search result navigation and back-to-origin.
- JPEG/PNG image scaling, SVG image references and cover rendering.
- Font scale 80/100/120/140/160/200%.
- Partial refresh, full refresh on open and periodic anti-ghosting.
- 240 MHz during book/chapter loading, 80 MHz baseline while reading.
- USB Disk Mode, deep sleep, battery learning and NTP clock.
- Performance cleanup: font/text caches, binary page-offset lookup,
  indexed ZIP/manifest/title caches and search text cache.
