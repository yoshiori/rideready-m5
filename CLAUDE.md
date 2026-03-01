# CLAUDE.md

## Build & Deploy
- `mise run build` — build firmware
- `mise run upload` — flash to M5Stack via /dev/ttyUSB0
- `mise exec -- pio test -e native` — run unit tests
- Upload may fail with "Wrong boot mode" — press reset button on M5Stack and retry

## Hardware
- **Board**: M5Stack Core ESP32 (original, not Core2/CoreS3)
- **PORT A (I2C)**: SDA=21, SCL=22
- **ENV III Sensor**: SHT30 (0x44) + QMP6988 (0x70)
- Speaker disabled via `M5.begin(true, true, true, false)`

## I2C Devices on PORT A
| Address | Device |
|---------|--------|
| 0x44 | SHT3X (temp/humidity) |
| 0x70 | QMP6988 (pressure) |

## Project Structure
- `lib/` — reusable libraries (auto-linked in native tests)
- `src/` — main firmware (excluded from native env build)
- `test/` — unit tests (`pio test -e native`)
- Testable code goes in `lib/`, not `src/`

## Known Pitfalls
- Arduino `RISING` macro conflicts with enum names — use `TREND_` prefix
- M5Unit-ENV: use `"SHT3X.h"` / `"QMP6988.h"` individually (no `M5_ENV.h`)
- QMP6988 address is 0x70 on this unit, not default 0x56

## Wi-Fi & NTP
- `src/wifi_config.h` contains SSID/PASS (`#define WIFI_SSID` / `WIFI_PASS`)
- `src/wifi_config.h.example` is the template (checked in); `src/wifi_config.h` is gitignored
- ESP32 built-in WiFi (no extra library needed)
- NTP: `configTime(9*3600, 0, "ntp.nict.jp", "pool.ntp.org")` — JST, no DST
- Blocking connect on boot (max 10s), non-blocking reconnect every 30s in loop
- NTP resync every 1 hour

## Maintenance Tracker
- **B button**: Reset Tire Pressure timer
- **C button**: Reset Chain Lube
- Cumulative uptime tracked via `millis()` and persisted to NVS every 60s
- When NTP is synced, resets also store Unix epoch for date-based display
- Display: NTP synced → "N days" (≤7) or "MM/DD" (>7); NTP not synced → "N h"
- Distance (`--- km`) is placeholder until Step 4 (Strava/GPS)

## NVS Keys (namespace: "rideready")
| Key | Type | Description |
|-----|------|-------------|
| `cum_uptime` | ULong64 | Cumulative uptime (ms) |
| `tire_reset` | ULong64 | Tire pressure reset cumulative uptime (ms) |
| `chain_reset` | ULong64 | Chain lube reset cumulative uptime (ms) |
| `tire_epoch` | ULong64 | Tire pressure reset Unix timestamp |
| `chain_epoch` | ULong64 | Chain lube reset Unix timestamp |

## Screen Layout (320x240)
```
+------------------+------------------+
| (0,0)-(159,119)  | (160,0)-(319,119)|
|  ENV Panel       |  INFO Panel      |
|  25.3 C          |  14:32           |
|  48.2 %          |  2026/03/02      |
|  1014 -          |  WiFi: MySSID    |
|  hPa             |  IP: 192.168.x.x|
+------------------+------------------+
| (0,120)-(319,239)                   |
|  Maintenance Panel                  |
|  Tire Pressure / Chain Lube         |
+-------------------------------------+
```
