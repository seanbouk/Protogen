# Protogen (LumiFur fork)

Fork of [stef1949/LumiFur_Controller](https://github.com/stef1949/LumiFur_Controller) (BSD-3), running on an
Adafruit MatrixPortal S3 (ESP32-S3, 8MB flash, 2MB PSRAM) driving two Waveshare HUB75 panels.

- **Build env:** `adafruit_matrixportal_esp32s3_waveshare` (what the board shipped with, `-O2`).
  `python -m platformio run -e adafruit_matrixportal_esp32s3_waveshare`
- **Port:** COM4 (USB ID 303A:1001, native USB-Serial/JTAG).
- **Remotes:** `origin` = seanbouk/Protogen (public), `upstream` = stef1949/LumiFur_Controller.
- **Baseline:** tag `board-original-5.0.2` = upstream commit `48f033e`, the closest match to the firmware
  found on the board (v5.0.2, built 1 Oct 2026).

## Safety
- Full flash dump of the original board: `backup/original_firmware_full_8MB.bin` (git-ignored; also copied to
  `~/Documents/Protogen-firmware-backup/`). SHA-256 `0cf5adce8e7830c7c44aaf1ab817ac27c47924c0cdf1466987afcd0a4e4a8ce9`.
  Never commit it - the repo is public and the NVS partition may hold BLE bond keys.
- Restore everything exactly as it came:
  `python -m esptool --port COM4 -b 921600 write-flash 0 backup/original_firmware_full_8MB.bin`
  If the board won't connect: hold BOOT, tap RESET, release BOOT, then run the command.
- Ask before flashing. Prefer `pio run -t upload` (app only) over touching the filesystem partition, so the
  `ffat` partition (videos) survives.
