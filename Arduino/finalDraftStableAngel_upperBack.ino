#include <WiFi.h>
#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "secrets.h"

MPU6050 mpu;

// ---------------- DEVICE ----------------
#define DEVICE_ID     "upper"   // change to "lower" on the other ESP32

// ---------------- MOTOR ----------------
#define MOTOR_PIN 18

// ---------------- POSTURE WINDOW ----------------
const float GOOD_MIN = -18.0;
const float GOOD_MAX = -9.0;

// Must be bad for this long before vibrating
const int HOLD_MS = 700;

unsigned long badStart = 0;
unsigned long lastCloudSend = 0;

// ---------------- FILTER SETTINGS ----------------
const int NUM_SAMPLES = 8;               // moving average size
const float MOTION_THRESHOLD = 1.2;      // degrees; update only if bigger than this
const float HOLD_STILL_THRESHOLD = 0.35; // if avg pitch changes less than this, treat as still
const float ROUND_STEP = 0.5;            // round shown value to nearest 0.5 degree

float pitchBuffer[NUM_SAMPLES];
int pitchIndex = 0;
bool bufferFilled = false;

float avgPitch = 0.0;
float displayPitch = 0.0;
float lastAvgPitch = 0.0;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---------- WiFi connect ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
    if (millis() - start > 20000) {
      Serial.println("\nWiFi TIMEOUT (20s). Check SSID/password.");
      return;
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ---------- Firebase init ----------
void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Requires: Firebase Console -> Authentication -> Sign-in method -> Anonymous -> Enable
  Serial.println("Firebase signUp (anonymous)...");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signUp OK");
  } else {
    Serial.print("Firebase signUp FAILED: ");
    Serial.println(config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setBSSLBufferSize(4096, 1024);
}

// ---------- Write online status ----------
void writeStatusOnline() {
  String statusPath = String("devices/") + DEVICE_ID + "/status";
  Firebase.RTDB.setString(&fbdo, statusPath.c_str(), "online");
}

// ---------- Read raw pitch ----------
float readPitch() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  float fax = (float)ax;
  float fay = (float)ay;
  float faz = (float)az;

  return atan2(-fax, sqrt(fay * fay + faz * faz)) * 180.0 / PI;
}

// ---------- Round to nearest step ----------
float roundToStep(float value, float step) {
  return round(value / step) * step;
}

// ---------- Add sample to buffer ----------
void addPitchSample(float p) {
  pitchBuffer[pitchIndex] = p;
  pitchIndex++;

  if (pitchIndex >= NUM_SAMPLES) {
    pitchIndex = 0;
    bufferFilled = true;
  }
}

// ---------- Compute average ----------
float getAveragePitch() {
  int count = bufferFilled ? NUM_SAMPLES : pitchIndex;
  if (count == 0) return 0.0;

  float sum = 0.0;
  for (int i = 0; i < count; i++) {
    sum += pitchBuffer[i];
  }
  return sum / count;
}

void setup() {
  Serial.begin(115200);

  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  Wire.begin(21, 22);
  mpu.initialize();

  if (mpu.testConnection()) {
    Serial.println("MPU6050 connected");
  } else {
    Serial.println("MPU6050 failed");
    while (1) delay(10);
  }

  // Fill buffer initially
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float p = readPitch();
    pitchBuffer[i] = p;
    delay(20);
  }
  bufferFilled = true;
  pitchIndex = 0;

  avgPitch = getAveragePitch();
  lastAvgPitch = avgPitch;
  displayPitch = roundToStep(avgPitch, ROUND_STEP);

  connectWiFi();
  setupFirebase();
  writeStatusOnline();
}

void loop() {
  float rawPitch = readPitch();
  addPitchSample(rawPitch);

  avgPitch = getAveragePitch();

  float avgChange = fabs(avgPitch - lastAvgPitch);
  float displayChange = fabs(avgPitch - displayPitch);

  // Only update shown angle if there is meaningful movement
  if (avgChange > HOLD_STILL_THRESHOLD && displayChange > MOTION_THRESHOLD) {
    displayPitch = roundToStep(avgPitch, ROUND_STEP);
  }

  lastAvgPitch = avgPitch;

  bool isGood = (displayPitch >= GOOD_MIN && displayPitch <= GOOD_MAX);
  bool isBadNow = !isGood;

  if (isBadNow) {
    if (badStart == 0) badStart = millis();
  } else {
    badStart = 0;
  }

  bool vibrate = (badStart != 0 && (millis() - badStart) >= HOLD_MS);

  digitalWrite(MOTOR_PIN, vibrate ? HIGH : LOW);

  Serial.print("raw=");
  Serial.print(rawPitch, 2);
  Serial.print("  avg=");
  Serial.print(avgPitch, 2);
  Serial.print("  shown=");
  Serial.print(displayPitch, 2);
  Serial.print("  good=");
  Serial.print(isGood);
  Serial.print("  motor=");
  Serial.println(vibrate);

  if (millis() - lastCloudSend >= 250) {
    lastCloudSend = millis();

    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    String path = String("devices/") + DEVICE_ID + "/current";

    FirebaseJson json;
    json.set("pitch", displayPitch);
    json.set("avgPitch", avgPitch);
    json.set("good", isGood ? 1 : 0);
    json.set("motor", vibrate ? 1 : 0);
    json.set("ms", (int)millis());

    if (!Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
      Serial.print("RTDB write FAILED: ");
      Serial.println(fbdo.errorReason());
    }
  }

  delay(100);
}