#include <WiFi.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define WIFI_SSID     "Wifi"
#define WIFI_PASSWORD "Password"

#define API_KEY       "API_Key"
#define DATABASE_URL  "firebase website"
#define DEVICE_ID     "lower"   


const byte MPU_ADDR = 0x68;  
int16_t ax, ay, az;


float offset = 0.0f;


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastSend = 0;


void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
    if (millis() - start > 20000) {
      Serial.println("\nWiFi TIMEOUT (20s) - check SSID/password.");
      return;
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}


void initMPU() {
  Wire.begin(21, 22); 

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);     
  Wire.write(0x00);     
  Wire.endTransmission(true);

  delay(200);
}


void readAccel() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); 
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDR, 6, true);

  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
}




float getAngleDeg() {
  readAccel();
  float angle = atan2((float)ax, (float)az) * 180.0 / PI;
  return angle;
}


float calibrateOffset(int samples = 50) {
  Serial.println("Hold upright — calibrating...");
  delay(1500);

  float sum = 0.0f;
  for (int i = 0; i < samples; i++) {
    sum += getAngleDeg();
    delay(20);
  }
  float avg = sum / samples;

  Serial.print("Offset = ");
  Serial.println(avg);
  return avg;
}


void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  
  
  Serial.println("Signing up (anonymous)...");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signUp OK");
  } else {
    Serial.print("Firebase signUp FAILED: ");
    Serial.println(config.signer.signupError.message.c_str());
    Serial.println("If your RTDB Rules require auth, enable Anonymous auth in Firebase console.");
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  
  fbdo.setBSSLBufferSize(4096, 1024);
}


void writeStatusOnline() {
  String statusPath = String("devices/") + DEVICE_ID + "/status";
  if (Firebase.RTDB.setString(&fbdo, statusPath.c_str(), "online")) {
    Serial.println("RTDB status OK -> /" + statusPath);
  } else {
    Serial.print("RTDB status FAILED: ");
    Serial.println(fbdo.errorReason());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- BOOT ---");

  connectWiFi();
  initMPU();
  offset = calibrateOffset();

  setupFirebase();

  
  writeStatusOnline();
}

void loop() {
  
  if (millis() - lastSend < 250) return;
  lastSend = millis();

  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    connectWiFi();
  }

  
  float rawAngle = getAngleDeg();
  float angle = rawAngle - offset;

  
  const float THRESH_DEG = 20.0;  
  int badPosture = (fabs(angle) > THRESH_DEG) ? 1 : 0;

  
  String path = String("devices/") + DEVICE_ID + "/current";

  FirebaseJson json;
  json.set("angle", angle);
  json.set("badPosture", badPosture);
  json.set("ms", (int)millis());

  Serial.print("RTDB write -> /");
  Serial.println(path);

  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("Sent OK");
  } else {
    Serial.print("Sent FAILED: ");
    Serial.println(fbdo.errorReason());
  }

  
  String lastSeenPath = String("devices/") + DEVICE_ID + "/lastSeenMs";
  Firebase.RTDB.setInt(&fbdo, lastSeenPath.c_str(), (int)millis());
}