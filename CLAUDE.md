# Protogen (LumiFur fork)

Fork of [stef1949/LumiFur_Controller](https://github.com/stef1949/LumiFur_Controller) (BSD-3), running on an
Adafruit MatrixPortal S3 (ESP32-S3, 8MB flash, 2MB PSRAM) driving two Waveshare HUB75 panels.

- **Build env:** `protogen` (our own env in `platformio.ini`; default). `python -m platformio run -e protogen`
  The board shipped built with upstream's `adafruit_matrixportal_esp32s3_waveshare` env, whose green/blue pin swap
  is wrong for these panels - don't use it.
- **Panel diagnostics:** `python -m platformio run -e panel-test` (`src/diagnostics/panel_test.cpp`; findings in
  its header). Serial commands drive it; open COM4 with dtr/rts False.
- **Port:** COM4 (USB ID 303A:1001, native USB-Serial/JTAG).
- **Remotes:** `origin` = seanbouk/Protogen (public), `upstream` = stef1949/LumiFur_Controller.
- **Baseline:** tag `board-original-5.0.2` = upstream commit `48f033e`, the closest match to the firmware
  found on the board (v5.0.2, built 1 Oct 2026).

## Private folder
`Protogen-Mask-Private/` is git-ignored and backed up to Google Drive by Sean (don't touch Drive yourself).
It holds `backups/`, `builds/` (numbered `.uf2` files his daughter drags onto the board) and `secrets/`.
See its README. Never move anything from it into git.

## Safety
- Full flash dump of the original board: `Protogen-Mask-Private/backups/original_firmware_full_8MB.bin`
  (also copied to `~/Documents/Protogen-firmware-backup/`). SHA-256
  `0cf5adce8e7830c7c44aaf1ab817ac27c47924c0cdf1466987afcd0a4e4a8ce9`. Its NVS partition holds the board's
  BLE identity/bond keys, so it must never be committed - the repo is public.
- Restore everything exactly as it came:
  `python -m esptool --port COM4 -b 921600 write-flash 0 Protogen-Mask-Private/backups/original_firmware_full_8MB.bin`
  If the board won't connect: hold BOOT, tap RESET, release BOOT, then run the command.
- Kid-friendly path: double-tap RESET -> MATRXS3BOOT drive -> drag a `.uf2`. Make UF2s with
  `python tools/bin2uf2.py in.bin out.uf2` (ESP32-S3 family, base 0x0, app partition only).
  `00-ORIGINAL-lumifur-5.0.2.uf2` is the app extracted from the dump. UF2 route tested and working.
  After a UF2 flash the board stays off USB-serial until a single RESET press.

## Hardware/firmware facts (learned 2026-11-10)
- Shipped firmware = upstream `48f033e` (dev branch, v5.0.2) + the seller's private "Buttons v3" mod, which we
  recreated (UP = face list, DOWN = 36 hues/rainbow cycle/rainbow plasma, NVS key `buttonClrV3`) + BLE icon hidden.
- **Must build with espressif32 6.x** (pinned 6.7.0 = Arduino 2.0.16 / IDF 4.4.7). IDF 5 apps (platform 7+/8) are
  rejected by the TinyUF2/IDF 4.4 bootloader, which falls back to the UF2 drive.
- Colour order is standard RGB (fixed by using the standard MatrixPortal pins in the `protogen` env).
- **Must build with `-DPIXEL_COLOR_DEPTH_BITS=7`**: HUB75 lib 3.1.0 picks its CIE table at compile time but LumiFur
  sets 7-bit depth at runtime; mismatched, the table's top bit is dropped and colours wrap (rainbow banding).
- Display: 16MHz, min refresh 120Hz, 7-bit -> ~205Hz, no flicker. Upstream's always-on "brightness boost"
  (`#ifdef` of a macro defined 0) forced 20MHz/40Hz; fixed to `#if`.
- Full white at full brightness browns out on desktop USB. A power budget/limiter is a candidate feature.
- Library versions are pinned in the `protogen` env; upgrade deliberately and re-run the panel test.
- Fitted: LIS3DH accelerometer (±4g, 50Hz), UP/DOWN buttons. Not fitted: APDS9960 proximity (firmware retries
  every few seconds), probably no mic. Display is two 64x32 panels = 128x32. FFat partition is empty (no videos).
- Flash app only: `python -m esptool --port COM4 -b 921600 write-flash 0x10000 .pio/build/<env>/firmware.bin`.
  Opening COM4 with pyserial defaults resets the board; set dtr/rts False before `open()`.
- Ask before flashing. Prefer `pio run -t upload` (app only) over touching the filesystem partition, so the
  `ffat` partition (videos) survives.
