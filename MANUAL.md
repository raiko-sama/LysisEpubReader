# Build and Flash Manual

Step-by-step guide for **Lysis Epub Reader V1.0.2** on the M5Stack Paper S3.

## 1. Requirements

- M5Stack Paper S3
- USB **data** cable
- microSD card formatted **FAT32**
- Python 3.10 or newer
- PlatformIO Core, or VS Code with the PlatformIO extension
- `esptool.py` from the PlatformIO environment, or `pip install esptool`

Check the tools:

```bash
python --version
pio --version
```

## 2. Get the source

```bash
git clone https://github.com/raiko-sama/LysisEpubReader.git
cd LysisEpubReader
```

## 3. Prepare the microSD card

1. Format the card as FAT32.
2. Create a `books/` directory in the root.
3. Copy `.epub` files into `books/`.
4. Safely eject the card and insert it into the Paper S3.

```text
/
└── books/
    ├── book-one.epub
    └── book-two.epub
```

The firmware creates `.state/` automatically for settings, reading state, bookmarks and battery history.

## 4. Build the firmware

Distribution build (recommended; no serial test CLI):

```bash
pio run -e release
```

Development build with the serial test CLI:

```bash
pio run -e m5stack-papers3
```

Main outputs for the release build:

- `.pio/build/release/firmware.bin` — application image
- `lysis-reader-1.0.2.bin` — merged 8 MB image for a complete flash

The development environment produces `.pio/build/m5stack-papers3/firmware.bin` and `lysis-reader-1.0.2-dev.bin`.

Clean build:

```bash
pio run -e release -t clean
pio run -e release
```

## 5. Connect the board

1. Connect the Paper S3 with a USB data cable.
2. Find the serial port:
   - Windows: Device Manager → Ports (COM & LPT)
   - Linux: `ls /dev/ttyACM*` or `ls /dev/ttyUSB*`
   - macOS: `ls /dev/cu.usbmodem*`
3. Note the port name.

## 6. Flash with PlatformIO

Replace `COM3` with your port:

```bash
pio run -e release -t upload --upload-port COM3
```

Wait for the success message and the automatic restart.

If the port is busy, close any serial monitor:

```bash
pio device monitor --port COM3
```

Press `Ctrl+C`, then retry the upload.

## 7. Flash the merged image

The merged image contains the bootloader, partition table and application:

```bash
esptool.py --chip esp32s3 -p COM3 -b 921600 write_flash 0x0 firmware/lysis-reader-1.0.2.bin
```

Use this for a complete recovery or a first installation from a blank device. It replaces the full flash content.

If the device stays in download mode after a merged flash, erase the otadata region and reboot:

```bash
esptool.py --chip esp32s3 -p COM3 erase_region 0xe000 0x2000
```

## 8. First boot

The device shows the boot screen with the firmware version at the bottom centre, then opens the library.

- Tap a book to open it.
- Tap the left or right page edge to turn pages.
- Swipe up while reading to open the menu.
- Swipe down to return to the library.
- Tap the status bar for chapter/bookmark controls or Go To page.

## 9. Reading and the rolling window

The reader keeps up to four chapters (text + layout) ready around the current page.

- Chapters at the boundary are switched from cache, without an "Opening chapter" screen during normal reading.
- If a neighbour is still cold, the page turn stays pending and is completed by the background worker.
- Eight pages per side are kept in normal reading; the window extends to twelve pages in the direction of a fast flip.
- After six consecutive flips in the same direction the CPU rises to 240 MHz; after three seconds without input it returns to 80 MHz and the window becomes symmetric again.

The reader stores the current chapter, page, bookmarks and layout settings.

## 10. Search

Search is **whole-word**:

- searching `cerca` does not match `ricerca` or `cercasi`;
- matching text is opened directly from the result list;
- **Back** returns to the page where the search was started.

Search runs on the core-0 I/O worker and uses a bounded chapter-text cache, so the UI stays responsive and repeating the same search is faster.

## 11. Battery behavior

The firmware estimates the remaining charge from a LiPo open-circuit-voltage curve and learned charge/discharge endpoints.

- While discharging, the displayed percentage is monotonic: it cannot increase because of voltage recovery after load removal.
- Charging and full-charge states use hysteresis.
- The last accepted percentage is persisted in `.state/_bat_cycle.dat`.
- Readings are appended to `.state/_bat_history.csv` for inspection and future model tuning.
- At the configured low-voltage cutoff the device performs a safe shutdown.

The battery is a 3.7 V / 1800 mAh single-cell LiPo charged by an LGS4056H.

## 12. Power-off behavior

When powered off:

- the display, speaker, IMU, WiFi, SD and SPI peripherals are stopped;
- the battery-backed BM8563 RTC keeps the clock running;
- the physical power button is the only way to turn the device back on.

No timer wake-up is enabled for the power-off fallback path.

## 13. USB Disk Mode

1. From the library, swipe up to open the menu.
2. Select **USB disk**.
3. Tap the screen to start the mode.
4. Copy books to the microSD from the computer.
5. Safely eject the volume.
6. Tap the screen to leave and reboot.

Do not unplug the cable while files are being written.

## 14. Updating

```bash
git pull
pio run -e release -t upload --upload-port COM3
```

For a full recovery, flash the updated merged image as described in section 7.

## 15. Host smoke test (optional)

The repository includes a headless GUI emulator used for host-side checks:

```bash
pwsh host/build_host.ps1
./host/host_gui.exe tests/host_smoke.txt
```

Expected output ends with `DONE frames=N` (the exact frame count depends on the test library).

## 16. Troubleshooting

### Serial port not found

- Use a data cable, not a charge-only cable.
- Try another USB port.
- Install the required USB serial driver.
- On Windows, check Device Manager.

### Upload fails or port is busy

```bash
pio device list
```

Close serial monitors, IDEs or other applications using the port, then retry.

### microSD not detected

- Confirm FAT32 formatting.
- Reinsert the card.
- Try a different card.
- Recreate the `books/` directory.

### Reader restarts or library does not appear

- Test with another EPUB file.
- Check that the book is not corrupted.
- Reflash the merged image.

### Build fails

```bash
pio run -e release -t clean
pio run -e release
```

Check that PlatformIO can download its dependencies.

### Some images are not displayed

JPEG and PNG are supported. Some formats, such as animated GIF, may not be decoded. This is a decoder limitation, not a library error.

## 17. Safety

- Do not remove the microSD during writes or updates.
- Do not interrupt a merged-image flash.
- Use firmware and libraries from this repository.

## 18. License

MIT. See [LICENSE](LICENSE).
