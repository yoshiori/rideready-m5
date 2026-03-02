#include <M5Stack.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>

#include "QMP6988.h"
#include "SHT3X.h"
#include "maintenance_display.h"
#include "maintenance_tracker.h"
#include "pressure_trend.h"
#include "strava_client.h"
#include "strava_config.h"
#include "weather_client.h"
#include "weather_config.h"
#include "wifi_config.h"

static const uint8_t ENV_SDA = 21;
static const uint8_t ENV_SCL = 22;
static const unsigned long READ_INTERVAL_MS = 10000;
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;
static const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;  // 1 hour
static const unsigned long NTP_CHECK_INTERVAL_MS = 5000;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
static const unsigned long STRAVA_SYNC_INTERVAL_MS = 600000;    // 10 min
static const unsigned long STRAVA_TOKEN_EXPIRY_BUFFER_SEC = 300;  // 5 min
static const unsigned long WEATHER_SYNC_INTERVAL_MS = 1800000;  // 30 min
static const unsigned long MANUAL_SYNC_COOLDOWN_MS = 30000;     // 30 sec
static const long GMT_OFFSET_SEC = 9 * 3600;                   // JST

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
unsigned long lastNtpCheckMs = 0;
unsigned long lastStravaSyncMs = 0;
float temperature = 0.0f;
float humidity = 0.0f;
float pressure_hpa = 0.0f;

// Cumulative uptime persisted across reboots
uint64_t cumulativeUptimeMs = 0;
unsigned long lastUptimeUpdateMs = 0;

bool ntpSynced = false;

// Strava state
char stravaAccessToken[256] = "";
char stravaRefreshToken[256] = "";
unsigned long stravaExpiresAt = 0;
StravaStats stravaStats = {};
StravaActivity stravaLatestActivity = {};
bool stravaDataValid = false;
bool stravaSyncNeeded = true;

// Chain lube distance tracking
float chainLubeDistanceKm = 0.0f;
bool chainLubeDistanceValid = false;

// Weather state
WeatherData weatherData = {};
bool weatherDataValid = false;
unsigned long lastWeatherSyncMs = 0;
unsigned long lastManualSyncMs = 0;
bool weatherSyncNeeded = true;

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
    Serial.printf("\nWiFi connected: %s\n",
                  WiFi.localIP().toString().c_str());
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

void updateResetEpoch(MaintenanceTracker& tracker) {
  if (ntpSynced) {
    time_t epochNow;
    time(&epochNow);
    tracker.setResetEpoch(epochNow);
  }
}

// --- Strava token management ---

void saveStravaTokens() {
  preferences.putString("strava_at", stravaAccessToken);
  preferences.putString("strava_rt", stravaRefreshToken);
  preferences.putULong64("strava_exp", static_cast<uint64_t>(stravaExpiresAt));
  Serial.println("Strava tokens saved to NVS");
}

void loadStravaTokens() {
  String at = preferences.getString("strava_at", "");
  String rt = preferences.getString("strava_rt", "");
  stravaExpiresAt = static_cast<unsigned long>(
      preferences.getULong64("strava_exp", 0));

  strncpy(stravaAccessToken, at.c_str(), sizeof(stravaAccessToken) - 1);
  stravaAccessToken[sizeof(stravaAccessToken) - 1] = '\0';
  strncpy(stravaRefreshToken, rt.c_str(), sizeof(stravaRefreshToken) - 1);
  stravaRefreshToken[sizeof(stravaRefreshToken) - 1] = '\0';

  // If no stored refresh token, use the one from config
  if (strlen(stravaRefreshToken) == 0) {
    strncpy(stravaRefreshToken, STRAVA_REFRESH_TOKEN,
            sizeof(stravaRefreshToken) - 1);
    stravaRefreshToken[sizeof(stravaRefreshToken) - 1] = '\0';
  }

  Serial.printf("Strava tokens loaded: AT=%s RT=%s\n",
                strlen(stravaAccessToken) > 0 ? "(set)" : "(empty)",
                strlen(stravaRefreshToken) > 0 ? "(set)" : "(empty)");
}

bool refreshStravaToken() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, "https://www.strava.com/oauth/token")) {
    Serial.println("Strava: Failed to begin HTTP");
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  char payload[512];
  snprintf(payload, sizeof(payload),
           "client_id=%s&client_secret=%s&grant_type=refresh_token&refresh_token=%s",
           STRAVA_CLIENT_ID, STRAVA_CLIENT_SECRET, stravaRefreshToken);

  int httpCode = http.POST(payload);
  Serial.printf("Strava token refresh: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    char newAt[256];
    char newRt[256];
    unsigned long newExp;

    if (StravaClient::parseTokenResponse(response.c_str(), newAt, sizeof(newAt),
                                          newRt, sizeof(newRt), newExp)) {
      strncpy(stravaAccessToken, newAt, sizeof(stravaAccessToken) - 1);
      strncpy(stravaRefreshToken, newRt, sizeof(stravaRefreshToken) - 1);
      stravaExpiresAt = newExp;
      saveStravaTokens();
      Serial.println("Strava token refreshed successfully");
      http.end();
      return true;
    }
  }

  http.end();
  Serial.println("Strava token refresh failed");
  return false;
}

bool ensureStravaToken() {
  if (strlen(stravaAccessToken) == 0 || strlen(stravaRefreshToken) == 0) {
    // No tokens at all, try refresh with config token
    return refreshStravaToken();
  }

  // Check if token is expired (with 5 min buffer)
  if (ntpSynced) {
    time_t now;
    time(&now);
    if (static_cast<unsigned long>(now) >= stravaExpiresAt - STRAVA_TOKEN_EXPIRY_BUFFER_SEC) {
      return refreshStravaToken();
    }
  } else {
    // Without NTP, refresh if access token seems stale
    if (stravaExpiresAt == 0) {
      return refreshStravaToken();
    }
  }

  return true;
}

// --- Strava API calls ---

bool fetchStravaStats(bool isRetry = false) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athletes/" +
               String(STRAVA_ATHLETE_ID) + "/stats";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava stats: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    bool ok = StravaClient::parseStats(response.c_str(), stravaStats);
    http.end();
    if (ok) {
      Serial.printf("Strava stats: %.1f km total, %d rides\n",
                    stravaStats.all_ride_totals_km, stravaStats.all_ride_count);
    }
    return ok;
  }

  if (httpCode == 401 && !isRetry) {
    Serial.println("Strava: Unauthorized, will refresh token");
    http.end();
    if (refreshStravaToken()) {
      return fetchStravaStats(true);  // Retry once after refresh
    }
    return false;
  }

  http.end();
  return false;
}

bool fetchStravaLatestActivity() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client,
                  "https://www.strava.com/api/v3/athlete/activities?per_page=1")) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava activities: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    bool ok =
        StravaClient::parseActivity(response.c_str(), stravaLatestActivity);
    http.end();
    if (ok) {
      Serial.printf("Strava latest: %s (%.1f km)\n",
                    stravaLatestActivity.name,
                    stravaLatestActivity.distance_km);
    }
    return ok;
  }

  http.end();
  return false;
}

bool fetchChainLubeDistance(bool isRetry = false) {
  time_t resetEpoch = chainLube.resetEpoch();
  if (resetEpoch == 0) {
    // No reset epoch recorded; cannot query by date
    chainLubeDistanceValid = false;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athlete/activities?after=" +
               String(static_cast<unsigned long>(resetEpoch)) + "&per_page=200";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava chain lube distance: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    float totalKm = 0.0f;
    bool ok = StravaClient::parseActivitiesDistance(response.c_str(), totalKm);
    http.end();
    if (ok) {
      chainLubeDistanceKm = totalKm;
      chainLubeDistanceValid = true;
      Serial.printf("Chain lube distance since reset: %.1f km\n", totalKm);
    }
    return ok;
  }

  if (httpCode == 401 && !isRetry) {
    Serial.println("Strava: Unauthorized, will refresh token");
    http.end();
    if (refreshStravaToken()) {
      return fetchChainLubeDistance(true);
    }
    return false;
  }

  http.end();
  return false;
}

void syncStrava() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Strava sync starting...");

  if (!ensureStravaToken()) {
    Serial.println("Strava: No valid token, skipping sync");
    return;
  }

  bool statsOk = fetchStravaStats();
  bool activityOk = fetchStravaLatestActivity();
  bool chainDistOk = fetchChainLubeDistance();

  if (statsOk || activityOk) {
    stravaDataValid = true;
  }

  Serial.printf("Strava sync done: stats=%s activity=%s chainDist=%s\n",
                statsOk ? "OK" : "FAIL", activityOk ? "OK" : "FAIL",
                chainDistOk ? "OK" : "FAIL");
}

// --- Weather API ---

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Weather fetch starting...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast"
               "?latitude=" WEATHER_LAT
               "&longitude=" WEATHER_LON
               "&current=wind_speed_10m,wind_direction_10m,weather_code"
               "&hourly=precipitation_probability"
               "&forecast_hours=4"
               "&timezone=Asia/Tokyo";

  if (!http.begin(client, url)) {
    Serial.println("Weather: Failed to begin HTTP");
    return;
  }

  int httpCode = http.GET();
  Serial.printf("Weather: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    if (WeatherClient::parseWeather(response.c_str(), weatherData)) {
      weatherDataValid = true;
      Serial.printf("Weather: %.0f km/h %s code=%d precip3h=%d%%\n",
                    weatherData.wind_speed_kmh,
                    WeatherClient::windDirectionToCompass(weatherData.wind_direction_deg),
                    weatherData.weather_code,
                    weatherData.precipitation_probability_3h);
    } else {
      Serial.println("Weather: Parse failed");
    }
  }

  http.end();
}

// --- Sensors ---

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

// --- Display ---

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
  M5.Lcd.printf("%.0f %s", pressure_hpa,
                trendSymbol(pressureTrend.direction()));

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 92);
  M5.Lcd.print("hPa");

  // Weather line (wind + precipitation)
  if (weatherDataValid) {
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 104);
    if (weatherData.precipitation_probability_3h >= 0) {
      M5.Lcd.printf("%.0fkm/h %s R3h:%d%%",
                    weatherData.wind_speed_kmh,
                    WeatherClient::windDirectionToCompass(weatherData.wind_direction_deg),
                    weatherData.precipitation_probability_3h);
    } else {
      M5.Lcd.printf("%.0fkm/h %s",
                    weatherData.wind_speed_kmh,
                    WeatherClient::windDirectionToCompass(weatherData.wind_direction_deg));
    }
  }
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
  } else {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.print("WiFi: Disconnected");
  }

  // Strava total distance
  M5.Lcd.setCursor(164, 86);
  if (stravaDataValid) {
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(164, 92);
    M5.Lcd.printf("%.0f km", stravaStats.all_ride_totals_km);
  } else {
    M5.Lcd.setTextColor(DARKGREY, BLACK);
    M5.Lcd.print("--- km");
  }
}

static uint16_t severityColor(Severity s) {
  switch (s) {
    case Severity::NORMAL:   return WHITE;
    case Severity::WARNING:  return YELLOW;
    case Severity::CRITICAL: return RED;
    default:                 return WHITE;
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

  M5.Lcd.setTextColor(severityColor(tireResult.severity), BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(24, 162);
  M5.Lcd.print(tireResult.text);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(16, 192);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.print("[B] Reset");

  // Chain Lube - right side (always distance-based)
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(184, 144);
  M5.Lcd.print("Chain Lube");

  MaintenanceDisplayResult chainResult =
      MaintenanceDisplay::formatDistance(chainLubeDistanceKm);
  M5.Lcd.setTextColor(severityColor(chainResult.severity), BLACK);
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
  preferences.putFloat("chain_dist", chainLubeDistanceKm);
}

void setup() {
  M5.begin(true, true, true, false);
  M5.Power.begin();

  Serial.printf("RideReady! [%s]\n", GIT_HASH);

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

  // Restore chain lube distance cache
  chainLubeDistanceKm = preferences.getFloat("chain_dist", 0.0f);
  if (chainLubeDistanceKm > 0.0f) {
    chainLubeDistanceValid = true;
  }

  // Restore Strava tokens
  loadStravaTokens();

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

    // Initial Strava sync
    syncStrava();
    lastStravaSyncMs = millis();
    stravaSyncNeeded = false;

    // Initial weather fetch
    fetchWeather();
    lastWeatherSyncMs = millis();
    weatherSyncNeeded = false;
  }

  drawEnvPanel();
  drawInfoPanel();
  drawMaintenancePanel();
}

void loop() {
  M5.update();

  unsigned long now = millis();

  // A button: manual fetch (weather + Strava) with cooldown
  if (M5.BtnA.wasReleased() && (now - lastManualSyncMs) >= MANUAL_SYNC_COOLDOWN_MS) {
    lastManualSyncMs = now;
    Serial.println("Manual sync triggered (A button)");
    fetchWeather();
    syncStrava();
    lastWeatherSyncMs = now;
    lastStravaSyncMs = now;
    weatherSyncNeeded = false;
    stravaSyncNeeded = false;
    drawEnvPanel();
    drawInfoPanel();
    drawMaintenancePanel();
    M5.update();  // Flush stale button state after long network ops
  }

  // B button: reset Tire Pressure timer
  else if (M5.BtnB.wasReleased()) {
    uint64_t cumNow = currentCumulativeMs();
    tirePressure.reset(cumNow);
    updateResetEpoch(tirePressure);
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Tire Pressure timer reset");
  }

  // C button: reset Chain Lube
  else if (M5.BtnC.wasReleased()) {
    uint64_t cumNow = currentCumulativeMs();
    chainLube.reset(cumNow);
    updateResetEpoch(chainLube);
    chainLubeDistanceKm = 0.0f;
    chainLubeDistanceValid = false;
    preferences.putFloat("chain_dist", 0.0f);
    saveToNvs();
    stravaSyncNeeded = true;
    drawMaintenancePanel();
    Serial.println("Chain Lube reset (distance cleared)");
  }

  // Wi-Fi reconnection (every 30s)
  if (WiFi.status() != WL_CONNECTED &&
      (now - lastWifiCheckMs) >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiCheckMs = now;
    stravaSyncNeeded = true;  // Re-sync Strava after reconnect
    weatherSyncNeeded = true;  // Re-sync weather after reconnect
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

  // Check for first NTP sync after WiFi connects (throttled to every 5s)
  if (!ntpSynced && WiFi.status() == WL_CONNECTED &&
      (now - lastNtpCheckMs) >= NTP_CHECK_INTERVAL_MS) {
    lastNtpCheckMs = now;
    ntpSynced = checkNtpSync();
    if (ntpSynced) {
      Serial.println("NTP synced");
      lastNtpSyncMs = now;
      drawInfoPanel();
    }
  }

  // Strava sync (periodic or after reconnect)
  bool periodicStravaSync = (now - lastStravaSyncMs) >= STRAVA_SYNC_INTERVAL_MS;
  if ((stravaSyncNeeded || periodicStravaSync) && WiFi.status() == WL_CONNECTED && ntpSynced) {
    stravaSyncNeeded = false;
    lastStravaSyncMs = now;
    syncStrava();
    drawInfoPanel();
    M5.update();  // Flush stale button state after long network ops
  }

  // Weather sync (periodic or after reconnect)
  bool periodicWeatherSync = (now - lastWeatherSyncMs) >= WEATHER_SYNC_INTERVAL_MS;
  if ((weatherSyncNeeded || periodicWeatherSync) && WiFi.status() == WL_CONNECTED) {
    weatherSyncNeeded = false;
    lastWeatherSyncMs = now;
    fetchWeather();
    drawEnvPanel();
    M5.update();  // Flush stale button state after long network ops
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
