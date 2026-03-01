#include <M5Stack.h>
#include <Wire.h>

#include "QMP6988.h"
#include "SHT3X.h"
#include "pressure_trend.h"

static const uint8_t ENV_SDA = 21;
static const uint8_t ENV_SCL = 22;
static const unsigned long READ_INTERVAL_MS = 10000;

SHT3X sht3x;
QMP6988 qmp6988;
PressureTrend pressureTrend;

bool sensorOk = false;
unsigned long lastReadMs = 0;
float temperature = 0.0f;
float humidity = 0.0f;
float pressure_hpa = 0.0f;

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

void setup() {
  M5.begin(true, true, true, false);
  M5.Power.begin();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(30, 100);
  M5.Lcd.println("RideReady!");

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
}

void loop() {
  M5.update();

  unsigned long now = millis();
  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;

    if (sensorOk) {
      readSensors();
      Serial.printf("Temp: %.1f C  Humidity: %.1f %%  Pressure: %.1f hPa  Trend: %s\n",
                    temperature, humidity, pressure_hpa,
                    trendSymbol(pressureTrend.direction()));
      drawEnvPanel();
    }
  }
}
