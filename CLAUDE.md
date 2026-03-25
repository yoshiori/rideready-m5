# CLAUDE.md

## Build & Deploy
- `mise run build` — build firmware
- `mise run upload` — flash to M5Stack via /dev/ttyUSB0
- `mise exec -- pio test -e native` — run unit tests
- Upload may fail with "Wrong boot mode" — press reset button on M5Stack and retry
- `./scripts/serial_monitor.sh [seconds] [device]` — read serial output (default 10s, /dev/ttyUSB0)

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
- All buttons use **long press (3 seconds)** to prevent accidental resets
- **A button (hold 3s)**: Reset Tire Change distance — GPIO39 ghost triggers are safely ignored by long-press detection
- **B button (hold 3s)**: Reset Tire Pressure timer
- **C button (hold 3s)**: Reset Chain Lube (distance + epoch)
- Cumulative uptime tracked via `millis()` and persisted to NVS every 60s
- When NTP is synced, resets also store Unix epoch for date-based display
- **Tire Pressure**: time-based display — NTP synced → "N days" (≤7) or "MM/DD" (>7); NTP not synced → "N h"
- **Tire Change**: distance-based display — Strava API fetches activities since reset, sums distance in km
  - Shows "0 km" until Strava sync completes after reset
- **Chain Lube**: distance-based display — Strava API fetches activities since reset, sums distance in km
  - Shows "0 km" until Strava sync completes after reset

## Rain Ride Detection
- After each Strava sync, checks if the latest ride was in rain
- Uses Open-Meteo Archive API (`archive-api.open-meteo.com/v1/archive`) with activity's date and location
- If activity has `start_latlng`, uses that; otherwise falls back to `WEATHER_LAT`/`WEATHER_LON`
- Rain threshold: ≥0.5mm precipitation in any hour of the ride day
- Only checks outdoor ride types: Ride, EBikeRide, GravelRide, MountainBikeRide
- If rain detected, forces chain lube severity to CRITICAL (RED) regardless of distance
- Flag persists in NVS (`rain_ride`) until C button reset clears it
- Dry rides do NOT clear the flag — only manual C button reset does

## Open-Meteo Weather API
- `src/weather_config.h` contains latitude/longitude (`#define WEATHER_LAT` / `WEATHER_LON`)
- `src/weather_config.h.example` is the template; `src/weather_config.h` is gitignored
- Free API, no authentication required (10,000 req/day limit)
- Endpoint: `https://api.open-meteo.com/v1/forecast` with `current` + `hourly` params
- Fetches: wind speed (km/h), wind direction (degrees→8-compass), weather code, 3h precipitation probability
- Sync interval: 30 minutes
- Displayed in ENV panel (CYAN, textSize 1) below hPa line
- `WiFiClientSecure` + `HTTPClient` with `setInsecure()` (same pattern as Strava)

## Strava API
- `src/strava_config.h` contains Client ID/Secret/Refresh Token/Athlete ID
- `src/strava_config.h.example` is the template; `src/strava_config.h` is gitignored
- OAuth2 refresh token flow: initial token obtained manually via browser
- Token auto-refresh before expiry (6h lifetime, 5min buffer)
- Tokens persisted in NVS (`strava_at`, `strava_rt`, `strava_exp`)
- Endpoints: `/athletes/{id}/stats` (total distance), `/athlete/activities?per_page=1` (latest ride), `/athlete/activities?after={epoch}&per_page=200` (chain lube + tire change distance)
- Sync interval: 10 minutes
- `WiFiClientSecure` + `HTTPClient` with `setInsecure()` (no cert pinning)
- `ArduinoJson` v7 for JSON parsing (`JsonDocument`, not deprecated `StaticJsonDocument`)

## NVS Keys (namespace: "rideready")
| Key | Type | Description |
|-----|------|-------------|
| `cum_uptime` | ULong64 | Cumulative uptime (ms) |
| `tire_reset` | ULong64 | Tire pressure reset cumulative uptime (ms) |
| `tchange_rst` | ULong64 | Tire change reset cumulative uptime (ms) |
| `chain_reset` | ULong64 | Chain lube reset cumulative uptime (ms) |
| `tire_epoch` | ULong64 | Tire pressure reset Unix timestamp |
| `tchange_epo` | ULong64 | Tire change reset Unix timestamp |
| `chain_epoch` | ULong64 | Chain lube reset Unix timestamp |
| `strava_at` | String | Strava access token |
| `strava_rt` | String | Strava refresh token |
| `strava_exp` | ULong64 | Strava token expires_at (Unix epoch) |
| `tchange_dist` | Float | Cached tire change distance since reset (km) |
| `chain_dist` | Float | Cached chain lube distance since reset (km) |
| `weekly_dist` | Float | Weekly distance cache (km) |
| `weekly_avg` | Float | Weekly average distance cache (km) |
| `month_dist` | Float | Monthly distance cache (km) |
| `month_elev` | Float | Monthly elevation cache (m) |
| `rain_ride` | Bool | Rain ride flag (persists until chain reset) |

## Screen Layout (320x240)
```
+---[Out]-----------+--[INFO]---WiFi--14:32-+
| (0,0)-(159,119)   | (160,0)-(319,119)     |
|      18.3 °C      |  [bike] 12345 km      |
| [wx] 5km/h SW 20% | Wk  80km              |
+─ ─ ─ ─ ─ ─ ─ ─ ─ + [████████░░░░|░░░░░]  |
| [Room]             |  Mo 1200km UP 8000m   |
| 25.3C  48.2%       |           Up: 5m32s  |
| 1014→hPa           |                      |
+--------------------+----------------------+
|--[Tire]------------------[Chain]---------|
|      [TIRE 36px]       [CHAIN 72px]      |
|  Air: 3 days              245 km         |
|  Tire: 1234 km                           |
| ◀A Tire▷  ◀B Air▷     ◀C Chain▷         |
+------------------------------------------+
```
- INFO panel progress bar: green fill up to current, | marker at weekly average
  - Below average: green only
  - Above average: green to avg + cyan for excess

## Maintenance Color Thresholds
- **Tire Pressure**: WHITE (0-6 days) → YELLOW (7-13 days) → RED (14+ days)
- **Tire Change**: WHITE (0-2999 km) → YELLOW (3000-4999 km) → RED (5000+ km)
- **Chain Lube**: WHITE (0-299 km) → YELLOW (300-399 km) → RED (400+ km)
- Severity logic lives in `lib/MaintenanceDisplay` (testable), color mapping in `main.cpp`

## Button Map (all long press 3s)
- **A button (hold 3s)**: Reset Tire Change distance — GPIO39 ghost triggers safely ignored by long-press detection
- **B button (hold 3s)**: Reset Tire Pressure timer
- **C button (hold 3s)**: Reset Chain Lube (distance + epoch)
