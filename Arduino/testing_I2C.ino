#include <Wire.h>

void scanI2C() {
  Serial.println("\nI2C scan starting...");
  int found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found I2C device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
      delay(5);
    }
  }

  if (found == 0) Serial.println("No I2C devices found. Check wiring/pins/power.");
  else Serial.println("Scan done.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);                 // important: gives time to open monitor

  // Your assumed wiring pins:
  Wire.begin(21, 22);          // SDA, SCL

  Serial.println("\n--- BOOT ---");
  scanI2C();
}

void loop() {
  delay(5000);                 // repeat every 5 seconds
  scanI2C();
}