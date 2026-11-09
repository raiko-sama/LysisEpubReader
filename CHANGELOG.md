# Changelog

## V1.01 Beta - 2026-11-08

- Reworked battery estimation with a LiPo OCV curve, learned endpoints and
  monotonic discharge tracking; discharging percentages can no longer rise
  because of voltage recovery.
- Added persistent last accepted percentage and battery history
  (`.state/_bat_history.csv`).
- Reinforced power-off: display, speaker, IMU, WiFi, SD and SPI are stopped,
  no timer wake source is enabled, and only the power button can restart.
- RTC/clock is synchronized before power-off.
- README and MANUAL rewritten in English and updated for V1.01 Beta.
- Repository, build and flash documentation ready for GitHub.

## V1.0 Beta - 2026-11-08

- EPUB reader for M5Stack Paper S3.
- Library, pagination, chapters, bookmarks and whole-word search.
- Search result navigation and back-to-origin.
- JPEG/PNG image scaling, SVG image references and cover rendering.
- Font scale 80/100/120/140/160/200%.
- Partial refresh, full refresh on open and periodic anti-ghosting.
- 240 MHz during book/chapter loading, 80 MHz baseline while reading.
- USB Disk Mode, deep sleep, battery learning and NTP clock.
- Performance cleanup: font/text caches, binary page-offset lookup, indexed ZIP/manifest/title caches and search text cache.
