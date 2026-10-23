# Board profiles

Both supported variants use ESP32-S3, 16 MiB flash, 8 MiB OPI PSRAM, native
USB Device and BOOT on GPIO0. Profiles are compile-time and share firmware.

| Trait | waveshare-147 | pocket-096 |
| --- | --- | --- |
| Board | Waveshare ESP32-S3-LCD-1.47, non-B | Pocket-Dongle-S3 / UICPAL 0.96 N16R8 |
| Display | ST7789, 172×320 | ST7735S, 80×160 |
| MOSI / CLK / CS | 45 / 40 / 42 | 11 / 10 / 12 |
| DC / RESET | 41 / 39 | 13 / 14 |
| Backlight | GPIO48, active high | hardwired; no GPIO |
| RGB | GPIO38, RGB order | absent; do not drive GPIO38 |
| Offsets X / Y | 34 / 0 | 26 / 1 |
| Pixel byte order | little endian | big endian / BGR panel |
| Pages | dashboard, history, diagnostics | dashboard, history, four diagnostic pages |

The two original physical units previously passed real second-write tests.
This cleanup only builds firmware; it does not repeat hardware validation.
USB serial is generated from the chip MAC. No actual unit identifiers are public.
