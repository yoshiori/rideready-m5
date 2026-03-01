#include <M5Stack.h>

void setup() {
  M5.begin();
  M5.Power.begin();
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(50, 100);
  M5.Lcd.println("RideReady!");
}

void loop() {
  M5.update();
}
