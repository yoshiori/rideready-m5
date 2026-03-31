# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Deploy
- `mise run build` — build firmware
- `mise run upload` — flash to M5Stack via /dev/ttyUSB0
- `mise exec -- pio test -e native` — run all unit tests
- `mise exec -- pio test -e native -f test_pressure_trend` — run a single test suite
- Upload may fail with "Wrong boot mode" — press reset button on M5Stack and retry
- `./scripts/serial_monitor.sh [seconds] [device]` — read serial output (default 10s, /dev/ttyUSB0)
- First-time setup: `mise run setup` to install PlatformIO CLI

## Architecture

**Bicycle maintenance dashboard** on M5Stack Core ESP32 (320x240 LCD). Monitors tire pressure age, tire/chain wear distance via Strava, and indoor/outdoor weather sensors.

### Data flow
`Strava API` / `Open-Meteo API` / `ENV III sensor` → **lib/ parsers** (pure logic, testable) → **main.cpp** (HTTP, NVS, display)

### lib/ — Pure logic libraries (all testable in native env)
| Library | Role |
|---------|------|
| `StravaClient` | Parse JSON responses from Strava API (stats, activities, tokens) |
| `WeatherClient` | Parse Open-Meteo JSON (current weather, historical precipitation) |
| `RainRideDetector` | Determine if an activity was a rain ride (checks type + weather) |
| `MaintenanceTracker` | Track elapsed time/distance for maintenance items |
| `MaintenanceDisplay` | Format display strings + calculate severity (NORMAL/WARNING/CRITICAL) |
| `PressureTrend` | Barometric pressure trend (rising/falling/stable) from rolling samples |

### src/main.cpp — Firmware monolith
All hardware interaction, WiFi/NTP, HTTP calls, NVS persistence, LCD rendering, and button handling live here. The lib/ parsers are called with raw JSON strings; main.cpp handles the HTTP transport.

### Test pattern
Each `lib/Foo` has a corresponding `test/test_foo/test_foo.cpp`. Tests use Unity framework and run natively (no ESP32 needed). The native env (`platformio.ini [env:native]`) excludes `src/` via `build_src_filter = -<*>`.

## Hardware
- **Board**: M5Stack Core ESP32 (original, not Core2/CoreS3)
- **PORT A (I2C)**: SDA=21, SCL=22 — SHT3X (0x44, temp/humidity) + QMP6988 (0x70, pressure)
- Speaker disabled via `M5.begin(true, true, true, false)`

## Known Pitfalls
- Arduino `RISING` macro conflicts with enum names — use `TREND_` prefix
- M5Unit-ENV: use `"SHT3X.h"` / `"QMP6988.h"` individually (no `M5_ENV.h`)
- QMP6988 address is 0x70 on this unit, not default 0x56
- ArduinoJson v7 — use `JsonDocument`, not deprecated `StaticJsonDocument`

## Config Files (all gitignored, `.example` templates checked in)
- `src/wifi_config.h` — `WIFI_SSID` / `WIFI_PASS`
- `src/strava_config.h` — Client ID/Secret/Refresh Token/Athlete ID
- `src/weather_config.h` — `WEATHER_LAT` / `WEATHER_LON`

## Wi-Fi & NTP
- NTP: `configTime(9*3600, 0, "ntp.nict.jp", "pool.ntp.org")` — JST, no DST
- Blocking connect on boot (max 10s), non-blocking reconnect every 30s in loop
- NTP resync every 1 hour

## External APIs

### Strava API
- OAuth2 refresh token flow; tokens persisted in NVS
- Token auto-refresh before expiry (6h lifetime, 5min buffer)
- Endpoints: `/athletes/{id}/stats`, `/athlete/activities?per_page=1`, `/athlete/activities?after={epoch}&per_page=200`
- Sync interval: 10 min; backoff 15 min on 429
- `WiFiClientSecure` + `HTTPClient` with `setInsecure()` (no cert pinning)

### Open-Meteo Weather API
- Free, no auth (10,000 req/day limit)
- Forecast: `api.open-meteo.com/v1/forecast` — wind (speed/direction), weather code, 3h precipitation probability
- Archive: `archive-api.open-meteo.com/v1/archive` — historical precipitation for rain ride detection
- Sync interval: 30 min
- `WiFiClientSecure` + `HTTPClient` with `setInsecure()` (same pattern as Strava)

## Maintenance Tracker
- All buttons use **long press (3 seconds)** to prevent accidental resets
- **B+C buttons (hold 3s simultaneously)**: Reset Tire Change distance — moved from Button A due to GPIO39 ghost trigger hardware bug
- **B button (hold 3s)**: Reset Tire Pressure timer
- **C button (hold 3s)**: Reset Chain Lube (distance + epoch + rain flag)
- Cumulative uptime tracked via `millis()` and persisted to NVS every 60s
- When NTP is synced, resets also store Unix epoch for date-based display
- **Tire Pressure**: time-based — NTP synced → "N days" (≤7) or "MM/DD" (>7); no NTP → "N h"
- **Tire Change / Chain Lube**: distance-based — Strava fetches activities since reset epoch, sums km

## Rain Ride Detection
- After each Strava sync, checks if latest ride was in rain via Open-Meteo Archive API
- Uses activity's `start_latlng` if available, otherwise falls back to `WEATHER_LAT`/`WEATHER_LON`
- Rain threshold: ≥0.5mm precipitation in any hour during the ride
- Only checks outdoor ride types: Ride, EBikeRide, GravelRide, MountainBikeRide
- If rain detected, forces chain lube severity to CRITICAL (RED) regardless of distance
- Flag persists in NVS (`rain_ride`) until C button reset clears it

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
| ◀B+C Tire▷ ◀B Air▷    ◀C Chain▷         |
+------------------------------------------+
```
- INFO panel progress bar: green fill up to current, | marker at weekly average
  - Below average: green only; above average: green to avg + cyan for excess
- Color theme: Dracula palette (defined as `COL_*` constants in main.cpp)

## Maintenance Color Thresholds
- **Tire Pressure**: WHITE (0-6 days) → YELLOW (7-13 days) → RED (14+ days)
- **Tire Change**: WHITE (0-2999 km) → YELLOW (3000-4999 km) → RED (5000+ km)
- **Chain Lube**: WHITE (0-299 km) → YELLOW (300-399 km) → RED (400+ km)
- Severity logic lives in `lib/MaintenanceDisplay` (testable), color mapping in `main.cpp`
