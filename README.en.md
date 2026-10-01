[🇮🇩 Bahasa Indonesia](README.md) | 🇬🇧 English

# ESP32-CAM (AI-Thinker) — MJPEG Web Streaming Server

A simple web camera server for the **AI-Thinker ESP32-CAM** board (classic ESP32 chip — **not** the ESP32-S3), using the `esp32-camera` library + the `esp_http_server` bundled with ESP-IDF.

This board runs as its own **WiFi Access Point (AP)** — it does not connect to your home router. Just connect your phone/laptop to the board's WiFi, then open a browser to see the camera stream.

## Why this project is separate from the ESP32-S3 project

There is another project (`esp32s3-cam`) written for the Waveshare ESP32-S3-CAM-OVxxxx board, using the `esp_video` library (the `LCD_CAM` peripheral, which only exists on the S3 chip). This AI-Thinker board has the **classic ESP32** chip, which has no such peripheral at all — so the firmware has to be completely different, using `esp32-camera` (the I2S peripheral) instead. The code of these two projects **cannot be swapped between them**.

## Project structure

```
esp32cam-aithinker/
├── CMakeLists.txt              # top-level project
├── sdkconfig.defaults          # PSRAM must be enabled (camera frame buffer is kept in PSRAM)
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml       # dependency: espressif/esp32-camera
    ├── camera_pins.h           # standard AI-Thinker camera pinout
    └── main.c                  # WiFi AP + camera init + HTTP server
```

## Hardware

- Board: **AI-Thinker ESP32-CAM** (ESP32-D0WD-V3 chip, 4MB PSRAM)
- Camera: **OV2640** (included on the board)
- **No onboard USB port** — you must use an external USB-to-TTL programmer (FTDI/CP2102/CH340), connected to the `U0R`/`U0T`/`5V`/`GND` pins

## The two different environments used in this project

| Tool | Needs `venv`? | Purpose |
|---|---|---|
| `idf.py` (set-target, build) | **No** — the ESP-IDF environment is already set up automatically in your terminal | Compile the firmware |
| `python -m esptool` | **Yes** — `esptool` is installed inside this project's `venv` | Flash the firmware to the board |
| `python -m serial.tools.miniterm` | **Yes** — same as above | View the serial log manually (optional, see the note in the Monitor section) |

## Initial setup (once only)

```bash
cd esp32cam-aithinker
idf.py set-target esp32
```

To change the hotspot name/password, edit it in `main/main.c`:

```c
#define AP_SSID       "ESP32CAM"
#define AP_PASSWORD   "12345678"   // WPA2, minimum 8 characters. Use "" for an AP without a password
```

## Build

```bash
idf.py build
```

## Flash — use `esptool` through the `venv`, and you MUST enter download mode manually first

This board does **not auto-reset** like modern dev boards. Every time you want to flash:

1. **Short pin GPIO0 to GND** (with a jumper wire)
2. Press the **RESET** button on the board (or replug the power) while GPIO0 is still shorted
3. Activate the venv, then run esptool:
   ```bash
   .\venv\Scripts\activate
   python -m esptool --chip esp32 -p COM5 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/esp32cam-aithinker.bin
   ```
   *(replace `COM5` with your programmer's port)*
4. After flashing finishes (`Hard resetting via RTS pin...` appears), **remove the GPIO0-GND short**
5. Press RESET once more so the board boots normally (not into download mode)

## Viewing the serial log

The classic ESP32 prints its ROM boot message at **74880** baud, then switches to **115200** once the firmware is running.

**The easiest way** — use `idf.py monitor` (no `venv` needed, the ESP-IDF environment handles this automatically):
```bash
idf.py -p COM5 monitor
```
Press `Ctrl+]` to exit.

**Manual alternative** if `idf.py monitor` gives you trouble — use `miniterm` through the `venv`:
```bash
.\venv\Scripts\activate
python -m serial.tools.miniterm COM5 115200
```

## How to use once the board is running

1. Open the WiFi settings on your phone/laptop
2. Find & connect to the SSID **`ESP32CAM`**, password **`12345678`**
3. Open a browser and go to:
   - `http://192.168.4.1/stream` — live MJPEG stream
   - `http://192.168.4.1/capture` — take a single JPEG photo

The IP `192.168.4.1` is **fixed**, it does not change (it is the default gateway IP when the ESP32 acts as an Access Point) — so there is no need to look up the IP on a router.

## Troubleshooting encountered so far

| Symptom | Cause | Solution |
|---|---|---|
| `No serial data received` when flashing | Board has not entered download mode | Short GPIO0-GND before flashing (see the Flash steps above) |
| Flash succeeds, but the serial monitor stays empty/garbled | Likely a brownout (the AI-Thinker board is current-hungry, especially when WiFi is active) | Change the USB cable, avoid hubs, or provide a separate external 5V supply apart from the programmer |
| Compile error `expected ')' before 'MACSTR'` | Missing `#include <esp_mac.h>` | Already fixed in the latest `main.c` |
| `esp_camera_init()` fails to allocate memory | PSRAM is not enabled | Make sure `sdkconfig.defaults` is applied (`CONFIG_SPIRAM=y`), run fullclean+build again if needed |
