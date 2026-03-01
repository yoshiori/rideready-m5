#include <M5Stack.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "QMP6988.h"
#include "SHT3X.h"
#include "maintenance_display.h"
#include "maintenance_tracker.h"
#include "pressure_trend.h"
#include "wifi_config.h"

static const uint8_t ENV_SDA = 21;
static const uint8_t ENV_SCL = 22;
static const unsigned long READ_INTERVAL_MS = 10000;
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;
static const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;  // 1 hour
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
static const long GMT_OFFSET_SEC = 9 * 3600;  // JST

SHT3X sht3x;
QMP6988 qmp6988;
PressureTrend pressureTrend;
Preferences preferences;

MaintenanceTracker tirePressure;
MaintenanceTracker chainLube;

bool sensorOk = false;
unsigned long lastReadMs = 0;
unsigned long lastNvsSaveMs = 0;
unsigned long lastWifiCheckMs = 0;
unsigned long lastNtpSyncMs = 0;
float temperature = 0.0f;
float humidity = 0.0f;
float pressure_hpa = 0.0f;

// Cumulative uptime persisted across reboots
uint64_t cumulativeUptimeMs = 0;
unsigned long lastUptimeUpdateMs = 0;

bool ntpSynced = false;

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

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("Connecting to WiFi: %s", WIFI_SSID);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection failed (will retry in loop)");
  }
}

void initNtp() {
  configTime(GMT_OFFSET_SEC, 0, "ntp.nict.jp", "pool.ntp.org");
  Serial.println("NTP configured (JST, ntp.nict.jp)");
}

bool checkNtpSync() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 100)) {
    if (timeinfo.tm_year > (2020 - 1900)) {
      return true;
    }
  }
  return false;
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

void drawInfoPanel() {
  // Top-right quadrant: (160,0)-(319,119)
  M5.Lcd.fillRect(160, 0, 160, 120, BLACK);
  M5.Lcd.drawRect(160, 0, 160, 120, WHITE);

  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(164, 4);
  M5.Lcd.println("INFO");

  if (ntpSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(164, 20);
      M5.Lcd.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

      M5.Lcd.setCursor(164, 46);
      M5.Lcd.printf("%04d/%02d/%02d", timeinfo.tm_year + 1900,
                     timeinfo.tm_mon + 1, timeinfo.tm_mday);
    }
  } else {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(164, 20);
    M5.Lcd.print("--:--");
    M5.Lcd.setCursor(164, 46);
    M5.Lcd.print("----/--/--");
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(164, 72);
  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.printf("WiFi: %s", WIFI_SSID);
    M5.Lcd.setCursor(164, 86);
    M5.Lcd.printf("IP: %s", WiFi.localIP().toString().c_str());
  } else {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.print("WiFi: Disconnected");
  }
}

void drawMaintenancePanel() {
  // Bottom half: (0,120)-(319,239)
  M5.Lcd.fillRect(0, 120, 320, 120, BLACK);
  M5.Lcd.drawRect(0, 120, 320, 120, WHITE);

  uint64_t now = currentCumulativeMs();
  time_t currentEpoch = 0;
  if (ntpSynced) {
    time(&currentEpoch);
  }

  M5.Lcd.setTextColor(CYAN, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 124);
  M5.Lcd.print("MAINTENANCE");

  // Tire Pressure - left side
  uint64_t tireElapsedMs = 0;
  if (now > tirePressure.resetUptimeMs()) {
    tireElapsedMs = now - tirePressure.resetUptimeMs();
  }
  MaintenanceDisplayResult tireResult =
      MaintenanceDisplay::format(tirePressure.resetEpoch(), currentEpoch,
                                 tireElapsedMs);

  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(16, 144);
  M5.Lcd.print("Tire Pressure");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(24, 162);
  M5.Lcd.print(tireResult.text);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(16, 192);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.print("[B] Reset");

  // Chain Lube - right side
  uint64_t chainElapsedMs = 0;
  if (now > chainLube.resetUptimeMs()) {
    chainElapsedMs = now - chainLube.resetUptimeMs();
  }
  MaintenanceDisplayResult chainResult =
      MaintenanceDisplay::format(chainLube.resetEpoch(), currentEpoch,
                                 chainElapsedMs);

  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(184, 144);
  M5.Lcd.print("Chain Lube");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(192, 162);
  M5.Lcd.print(chainResult.text);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(184, 192);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.print("[C] Reset");
}

void saveToNvs() {
  unsigned long now_ms = millis();
  // Update cumulative uptime base
  cumulativeUptimeMs += (now_ms - lastUptimeUpdateMs);
  lastUptimeUpdateMs = now_ms;

  preferences.putULong64("cum_uptime", cumulativeUptimeMs);
  preferences.putULong64("tire_reset", tirePressure.resetUptimeMs());
  preferences.putULong64("chain_reset", chainLube.resetUptimeMs());
  preferences.putULong64("tire_epoch",
                          static_cast<uint64_t>(tirePressure.resetEpoch()));
  preferences.putULong64("chain_epoch",
                          static_cast<uint64_t>(chainLube.resetEpoch()));
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

  // Restore epoch values
  uint64_t tireEpoch = preferences.getULong64("tire_epoch", 0);
  uint64_t chainEpoch = preferences.getULong64("chain_epoch", 0);
  if (tireEpoch > 0) {
    tirePressure.setResetEpoch(static_cast<time_t>(tireEpoch));
  }
  if (chainEpoch > 0) {
    chainLube.setResetEpoch(static_cast<time_t>(chainEpoch));
  }

  Serial.printf(
      "NVS restored: cum_uptime=%llu tire_reset=%llu chain_reset=%llu "
      "tire_epoch=%llu chain_epoch=%llu\n",
      cumulativeUptimeMs, tireResetMs, chainResetMs, tireEpoch, chainEpoch);

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

  // Wi-Fi connection
  initWifi();

  // NTP time sync
  if (WiFi.status() == WL_CONNECTED) {
    initNtp();
    // Wait briefly for initial NTP sync
    delay(2000);
    ntpSynced = checkNtpSync();
    if (ntpSynced) {
      Serial.println("NTP synced");
    }
    lastNtpSyncMs = millis();
  }

  drawEnvPanel();
  drawInfoPanel();
  drawMaintenancePanel();
}

void loop() {
  M5.update();

  unsigned long now = millis();

  // B button: reset Tire Pressure timer
  if (M5.BtnB.wasPressed()) {
    uint64_t cumNow = currentCumulativeMs();
    tirePressure.reset(cumNow);
    if (ntpSynced) {
      time_t epochNow;
      time(&epochNow);
      tirePressure.setResetEpoch(epochNow);
    }
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Tire Pressure timer reset");
  }

  // C button: reset Chain Lube
  if (M5.BtnC.wasPressed()) {
    uint64_t cumNow = currentCumulativeMs();
    chainLube.reset(cumNow);
    if (ntpSynced) {
      time_t epochNow;
      time(&epochNow);
      chainLube.setResetEpoch(epochNow);
    }
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Chain Lube reset");
  }

  // Wi-Fi reconnection (every 30s)
  if (WiFi.status() != WL_CONNECTED &&
      (now - lastWifiCheckMs) >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiCheckMs = now;
    Serial.println("WiFi disconnected, attempting reconnect...");
    WiFi.reconnect();
  }

  // NTP resync (every 1 hour)
  if (WiFi.status() == WL_CONNECTED &&
      (now - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS) {
    lastNtpSyncMs = now;
    bool wasSynced = ntpSynced;
    ntpSynced = checkNtpSync();
    if (ntpSynced && !wasSynced) {
      Serial.println("NTP synced (recovered)");
    }
  }

  // Check for first NTP sync after WiFi connects
  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    ntpSynced = checkNtpSync();
    if (ntpSynced) {
      Serial.println("NTP synced");
      lastNtpSyncMs = now;
      drawInfoPanel();
    }
  }

  // Periodic sensor read
  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;

    if (sensorOk) {
      readSensors();
      Serial.printf(
          "Temp: %.1f C  Humidity: %.1f %%  Pressure: %.1f hPa  Trend: %s\n",
          temperature, humidity, pressure_hpa,
          trendSymbol(pressureTrend.direction()));
      drawEnvPanel();
    }

    drawInfoPanel();
    drawMaintenancePanel();
  }

  // Periodic NVS save (every 60s)
  if (now - lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
    lastNvsSaveMs = now;
    saveToNvs();
  }
}
