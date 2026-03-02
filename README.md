# RideReady M5

[![CI](https://github.com/yoshiori/rideready-m5/actions/workflows/ci.yml/badge.svg)](https://github.com/yoshiori/rideready-m5/actions/workflows/ci.yml)

A motorcycle dashboard firmware for **M5Stack Core ESP32** that displays environmental data, riding stats, weather forecasts, and maintenance reminders on a 320x240 LCD.

## Features

- **Environment monitoring** — Temperature, humidity, and barometric pressure via ENV III sensor (SHT30 + QMP6988) with pressure trend indicator
- **Weather forecast** — Wind speed/direction, weather code, and 3-hour precipitation probability from Open-Meteo API
- **Strava integration** — Total ride distance and latest activity via OAuth2 token refresh flow
- **Maintenance tracking** — Tire pressure (time-based) and chain lube (distance-based) reminders with color-coded severity (white/yellow/red)
- **Wi-Fi & NTP** — Auto-connect with non-blocking reconnect, NTP time sync for date display
- **Dracula color theme** — Clean UI with icon-based layout

## Hardware

| Component | Details |
|-----------|---------|
| Board | M5Stack Core ESP32 (original) |
| Sensor | ENV III Unit (SHT30 + QMP6988) on PORT A (I2C) |
| I2C Pins | SDA=21, SCL=22 |

## Screen Layout

```
+------------------+------------------+
|  ENV Panel       |  INFO Panel      |
|  25.3 C          |  14:32           |
|  48.2 %          |  2026/03/02      |
|  1014 -          |  WiFi: MySSID    |
|  hPa             |  1234 km         |
|  5km/h SW R3h:20%|                  |
+------------------+------------------+
|  Maintenance Panel                  |
|  Tire Pressure    Chain Lube        |
|     3 days         245 km           |
+-------------------------------------+
```

## Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE)
- [mise](https://mise.jdx.dev/) (optional, for task runner)

### Configuration

Copy the example config files and fill in your values:

```bash
cp src/wifi_config.h.example src/wifi_config.h
cp src/strava_config.h.example src/strava_config.h
cp src/weather_config.h.example src/weather_config.h
```

### Build & Flash

```bash
mise run build          # Build firmware
mise run upload         # Flash to M5Stack via /dev/ttyUSB0
```

Or with PlatformIO directly:

```bash
pio run -e m5stack-core-esp32
pio run -e m5stack-core-esp32 --target upload
```

> Upload may fail with "Wrong boot mode" — press the reset button on M5Stack and retry.

### Run Tests

```bash
mise exec -- pio test -e native
```

## Project Structure

```
src/           Main firmware (excluded from native test build)
lib/           Reusable libraries (auto-linked in native tests)
  MaintenanceTracker/   Time/distance tracking with NVS persistence
  MaintenanceDisplay/   Display formatting and severity thresholds
  PressureTrend/        Barometric pressure trend detection
  StravaClient/         Strava API JSON parsing
  WeatherClient/        Open-Meteo API JSON parsing
test/          Unit tests (PlatformIO Unity)
```

## Button Map

| Button | Action |
|--------|--------|
| A | Disabled (ESP32 GPIO39 errata) |
| B | Reset tire pressure timer |
| C | Reset chain lube (distance + epoch) |

## License

Private project.
