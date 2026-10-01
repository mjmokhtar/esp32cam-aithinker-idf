# ESP32-CAM (AI-Thinker) — MJPEG Web Streaming Server

🇮🇩 Bahasa Indonesia | [🇬🇧 English](README.en.md)

Web camera server sederhana untuk board **AI-Thinker ESP32-CAM** (chip ESP32 classic — **bukan** ESP32-S3), pakai library `esp32-camera` + `esp_http_server` bawaan ESP-IDF.

Board ini jalan sebagai **WiFi Access Point (AP)** sendiri — tidak connect ke router rumah. Cukup connect HP/laptop ke WiFi board ini, lalu buka browser untuk lihat stream kamera.

## Kenapa project ini terpisah dari project ESP32-S3

Ada project lain (`esp32s3-cam`) yang ditulis untuk board Waveshare ESP32-S3-CAM-OVxxxx, pakai library `esp_video` (peripheral `LCD_CAM`, cuma ada di chip S3). Board AI-Thinker ini chipnya **ESP32 classic**, tidak punya peripheral itu sama sekali — jadi firmware-nya harus beda total, pakai `esp32-camera` (peripheral I2S) sebagai gantinya. Dua project ini **tidak bisa saling ditukar kodenya**.

## Struktur project

```
esp32cam-aithinker/
├── CMakeLists.txt              # project top-level
├── sdkconfig.defaults          # wajib aktifkan PSRAM (frame buffer kamera disimpan di PSRAM)
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml       # dependency: espressif/esp32-camera
    ├── camera_pins.h           # pinout kamera standar AI-Thinker
    └── main.c                  # WiFi AP + init kamera + HTTP server
```

## Hardware

- Board: **AI-Thinker ESP32-CAM** (chip ESP32-D0WD-V3, 4MB PSRAM)
- Kamera: **OV2640** (bawaan board)
- **Tidak ada port USB onboard** — wajib pakai programmer USB-to-TTL eksternal (FTDI/CP2102/CH340), disambung ke pin `U0R`/`U0T`/`5V`/`GND`

## Dua environment berbeda yang dipakai di project ini

| Tool | Butuh `venv`? | Kegunaan |
|---|---|---|
| `idf.py` (set-target, build) | **Tidak** — environment ESP-IDF sudah otomatis siap di terminal Anda | Compile firmware |
| `python -m esptool` | **Ya** — `esptool` ter-install di dalam `venv` project ini | Flash firmware ke board |
| `python -m serial.tools.miniterm` | **Ya** — sama seperti di atas | Lihat log serial manual (opsional, lihat catatan di bagian Monitor) |

## Setup awal (sekali saja)

```bash
cd esp32cam-aithinker
idf.py set-target esp32
```

Kalau mau ganti nama/password hotspot, edit di `main/main.c`:

```c
#define AP_SSID       "ESP32CAM"
#define AP_PASSWORD   "12345678"   // WPA2, minimal 8 karakter. Isi "" untuk AP tanpa password
```

## Build

```bash
idf.py build
```

## Flash — pakai `esptool` lewat `venv`, WAJIB masuk mode download manual dulu

Board ini **tidak auto-reset** seperti board dev modern. Setiap kali mau flash:

1. **Short pin GPIO0 ke GND** (pakai jumper wire)
2. Tekan tombol **RESET** di board (atau colok ulang power) sambil GPIO0 masih di-short
3. Aktifkan venv, baru jalankan esptool:
   ```bash
   .\venv\Scripts\activate
   python -m esptool --chip esp32 -p COM5 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/esp32cam-aithinker.bin
   ```
   *(ganti `COM5` sesuai port programmer Anda)*
4. Setelah flash selesai (`Hard resetting via RTS pin...` muncul), **lepas short GPIO0-GND**
5. Tekan RESET sekali lagi supaya board boot normal (bukan mode download)

## Melihat log serial

Board classic ESP32 print boot message ROM di baud **74880**, baru switch ke **115200** setelah firmware jalan.

**Cara paling gampang** — pakai `idf.py monitor` (tidak butuh `venv`, environment ESP-IDF sudah handle ini otomatis):
```bash
idf.py -p COM5 monitor
```
Tekan `Ctrl+]` untuk keluar.

**Alternatif manual** kalau `idf.py monitor` bermasalah — pakai `miniterm` lewat `venv`:
```bash
.\venv\Scripts\activate
python -m serial.tools.miniterm COM5 115200
```

## Cara pakai setelah board menyala

1. Buka pengaturan WiFi di HP/laptop
2. Cari & connect ke SSID **`ESP32CAM`**, password **`12345678`**
3. Buka browser, akses:
   - `http://192.168.4.1/stream` — live MJPEG stream
   - `http://192.168.4.1/capture` — ambil 1 foto JPEG

IP `192.168.4.1` ini **tetap**, tidak berubah-ubah (default gateway IP saat ESP32 jadi Access Point) — jadi tidak perlu cari IP di router.

## Troubleshooting yang sudah pernah ditemui

| Gejala | Penyebab | Solusi |
|---|---|---|
| `No serial data received` saat flash | Board belum masuk mode download | Short GPIO0-GND sebelum flash (lihat langkah Flash di atas) |
| Flash sukses, tapi serial monitor kosong/garbled terus | Kemungkinan brownout (board AI-Thinker rakus arus, terutama saat WiFi aktif) | Ganti kabel USB, jangan lewat hub, atau kasih suplai 5V eksternal terpisah dari programmer |
| Error compile `expected ')' before 'MACSTR'` | Lupa `#include <esp_mac.h>` | Sudah difix di `main.c` versi terbaru |
| `esp_camera_init()` gagal alokasi memori | PSRAM belum aktif | Pastikan `sdkconfig.defaults` ter-apply (`CONFIG_SPIRAM=y`), fullclean+build ulang kalau perlu |

