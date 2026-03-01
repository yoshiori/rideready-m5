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

## Screen Layout (320x240)
```
+------------------+------------------+
| (0,0)-(159,119)  | (160,0)-(319,119)|
|  ENV Panel       |  (future)        |
+------------------+------------------+
| (0,120)-(319,239)                   |
|  (future)                           |
+-------------------------------------+
```
