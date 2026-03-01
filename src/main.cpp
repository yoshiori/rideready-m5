#include <M5Stack.h>
#include <Preferences.h>
#include <Wire.h>

#include "QMP6988.h"
#include "SHT3X.h"
#include "maintenance_tracker.h"
#include "pressure_trend.h"

static const uint8_t ENV_SDA = 21;
static const uint8_t ENV_SCL = 22;
static const unsigned long READ_INTERVAL_MS = 10000;
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000;

SHT3X sht3x;
QMP6988 qmp6988;
PressureTrend pressureTrend;
Preferences preferences;

MaintenanceTracker tirePressure;
MaintenanceTracker chainLube;

bool sensorOk = false;
unsigned long lastReadMs = 0;
unsigned long lastNvsSaveMs = 0;
float temperature = 0.0f;
float humidity = 0.0f;
float pressure_hpa = 0.0f;

// Cumulative uptime persisted across reboots
uint64_t cumulativeUptimeMs = 0;
unsigned long lastUptimeUpdateMs = 0;

static uint64_t currentCumulativeMs() {
  return cumulativeUptimeMs + (millis() - lastUptimeUpdateMs);
}

static const char* trendSymbol(TrendDirection dir) {
  switch (dir) {
    case TrendDirection::TREND_RISING:
      return "^";
    case TrendDirection::TREND_FALLING:
      return "v";
    default:
      return "-";
  }
}

void readSensors() {
  if (sht3x.update()) {
    temperature = sht3x.cTemp;
    humidity = sht3x.humidity;
  } else {
    Serial.println("Error: Failed to read SHT3X");
  }

  if (qmp6988.update()) {
    pressure_hpa = qmp6988.pressure / 100.0f;
    pressureTrend.addSample(pressure_hpa);
  } else {
    Serial.println("Error: Failed to read QMP6988");
  }
}

void drawEnvPanel() {
  // Top-left quadrant: (0,0)-(159,119)
  M5.Lcd.fillRect(0, 0, 160, 120, BLACK);
  M5.Lcd.drawRect(0, 0, 160, 120, WHITE);

  if (!sensorOk) {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 10);
    M5.Lcd.println("ENV Sensor");
    M5.Lcd.setCursor(4, 22);
    M5.Lcd.println("Init Failed!");
    return;
  }

  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);

  M5.Lcd.setCursor(4, 4);
  M5.Lcd.println("ENV");

  M5.Lcd.setTextSize(2);

  M5.Lcd.setCursor(4, 20);
  M5.Lcd.printf("%.1f C", temperature);

  M5.Lcd.setCursor(4, 46);
  M5.Lcd.printf("%.1f %%", humidity);

  M5.Lcd.setCursor(4, 72);
  M5.Lcd.printf("%.0f %s", pressure_hpa, trendSymbol(pressureTrend.direction()));

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 92);
  M5.Lcd.print("hPa");
}

void drawMaintenancePanel() {
  // Bottom half: (0,120)-(319,239)
  M5.Lcd.fillRect(0, 120, 320, 120, BLACK);
  M5.Lcd.drawRect(0, 120, 320, 120, WHITE);

  uint64_t now = currentCumulativeMs();

  M5.Lcd.setTextColor(CYAN, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 124);
  M5.Lcd.print("MAINTENANCE");

  // Tire Pressure - left side
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(16, 144);
  M5.Lcd.print("Tire Pressure");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(24, 162);
  uint32_t tireHours = tirePressure.elapsedHours(now);
  M5.Lcd.printf("%u h", tireHours);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(16, 192);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.print("[B] Reset");

  // Chain Lube - right side
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(184, 144);
  M5.Lcd.print("Chain Lube");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(192, 162);
  M5.Lcd.print("--- km");

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(184, 192);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.print("[C] Reset");
}

void saveToNvs() {
  uint64_t now = currentCumulativeMs();
  // Update cumulative uptime base
  cumulativeUptimeMs = now;
  lastUptimeUpdateMs = millis();

  preferences.putULong64("cum_uptime", cumulativeUptimeMs);
  preferences.putULong64("tire_reset", tirePressure.resetUptimeMs());
  preferences.putULong64("chain_reset", chainLube.resetUptimeMs());
}

void setup() {
  M5.begin(true, true, true, false);
  M5.Power.begin();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(30, 100);
  M5.Lcd.println("RideReady!");

  // NVS: restore cumulative uptime and maintenance reset times
  preferences.begin("rideready", false);
  cumulativeUptimeMs = preferences.getULong64("cum_uptime", 0);
  lastUptimeUpdateMs = millis();

  uint64_t tireResetMs = preferences.getULong64("tire_reset", 0);
  uint64_t chainResetMs = preferences.getULong64("chain_reset", 0);
  tirePressure.reset(tireResetMs);
  chainLube.reset(chainResetMs);

  Serial.printf("NVS restored: cum_uptime=%llu tire_reset=%llu chain_reset=%llu\n",
                cumulativeUptimeMs, tireResetMs, chainResetMs);

  bool sht_ok = sht3x.begin(&Wire, 0x44, ENV_SDA, ENV_SCL, 400000U);
  bool qmp_ok = qmp6988.begin(&Wire, 0x70, ENV_SDA, ENV_SCL, 400000U);
  sensorOk = sht_ok && qmp_ok;

  Serial.printf("SHT3X: %s, QMP6988: %s\n", sht_ok ? "OK" : "FAIL",
                qmp_ok ? "OK" : "FAIL");

  if (!sensorOk) {
    Serial.println("ENV sensor init failed");
  }

  // Read sensors immediately on boot
  if (sensorOk) {
    readSensors();
  }

  drawEnvPanel();
  drawMaintenancePanel();
}

void loop() {
  M5.update();

  unsigned long now = millis();

  // B button: reset Tire Pressure timer
  if (M5.BtnB.wasPressed()) {
    uint64_t cumNow = currentCumulativeMs();
    tirePressure.reset(cumNow);
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Tire Pressure timer reset");
  }

  // C button: reset Chain Lube
  if (M5.BtnC.wasPressed()) {
    uint64_t cumNow = currentCumulativeMs();
    chainLube.reset(cumNow);
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Chain Lube reset");
  }

  // Periodic sensor read
  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;

    if (sensorOk) {
      readSensors();
      Serial.printf("Temp: %.1f C  Humidity: %.1f %%  Pressure: %.1f hPa  Trend: %s\n",
                    temperature, humidity, pressure_hpa,
                    trendSymbol(pressureTrend.direction()));
      drawEnvPanel();
    }

    drawMaintenancePanel();
  }

  // Periodic NVS save (every 60s)
  if (now - lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
    lastNvsSaveMs = now;
    saveToNvs();
  }
}
