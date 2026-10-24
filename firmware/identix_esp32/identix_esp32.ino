/*
 * IDentix — ESP32 Edge Node Firmware
 * -----------------------------------
 * Reads heart rate (MAX30100 PPG) and skin temperature (DS18B20),
 * triggered by a capacitive touch sensor, and evaluates a simple
 * threshold-based "Recognized / Unknown" decision. Results are shown
 * on a 16x2 I2C LCD, signalled via LEDs/buzzer, and POSTed to a
 * backend server for logging and dashboard display.
 *
 * Libraries required (install via Arduino Library Manager):
 *   - LiquidCrystal I2C (by Frank de Brabander / Marco Schwartz)
 *   - MAX30100lib (by OXullo Intersecans)
 *   - OneWire
 *   - DallasTemperature
 *   (WiFi, HTTPClient, WiFiClientSecure ship with the ESP32 board package)
 *
 * Setup:
 *   1. Copy secrets.example.h -> secrets.h and fill in your WiFi + server details.
 *   2. Select your ESP32 board + correct COM port in the Arduino IDE.
 *   3. Upload.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "MAX30100_PulseOximeter.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, BACKEND_HOST, BACKEND_PATH

// --- PIN DEFINITIONS ---
#define TOUCH_PIN 32   // Touch Sensor I/O
#define TEMP_PIN  33   // DS18B20 Yellow Wire
#define BUZZER_PIN 2   // Buzzer Positive
#define LED_RED   4    // Access Denied
#define LED_GREEN 12   // Access Granted
#define LED_BLUE  14   // System Processing

// --- OBJECTS ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
PulseOximeter pox;
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// --- VARIABLES ---
float currentBPM = 0;
float currentTemp = 0;
const float TEMP_CALIBRATION_OFFSET = 3.0; // Bumped slightly for cold rooms

// Identifies the user this scan belongs to. In the current design this stays
// generic on the firmware side — the web dashboard is the one that knows
// *who* is scanning (it sets Firebase's "currentSession" node after PIN
// verification). See README "Known limitations" for how to wire this up
// end-to-end if you want the ESP32 to know the username too.
String currentUser = "unknown_user";

void onBeatDetected() {
  Serial.println("Beat!");
}

void setup() {
  Serial.begin(115200);

  // Pin Modes
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // Ensure LEDs are off at startup
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("IDENTIX SYSTEM");

  // --- CONNECT TO WI-FI ---
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.print("IDENTIX SYSTEM");
  lcd.setCursor(0, 1);
  lcd.print("WIFI CONNECTED");
  delay(1500);

  // Initialize Sensors
  Serial.print("Initializing MAX30100..");
  if (!pox.begin()) {
    Serial.println("FAILED");
  } else {
    Serial.println("SUCCESS");
  }
  pox.setOnBeatDetectedCallback(onBeatDetected);
  sensors.begin();

  // Startup Beep Sequence
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);

  resetSystem();
}

void loop() {
  // Idle state: Wait for Touch Sensor trigger
  if (digitalRead(TOUCH_PIN) == HIGH) {
    startAuthentication();
  }
}

void startAuthentication() {
  lcd.clear();
  lcd.print("SCANNING...");
  lcd.setCursor(0, 1);
  lcd.print("Keep finger still");
  digitalWrite(LED_BLUE, HIGH);

  // Scan Pulse uninterrupted for 8 seconds
  unsigned long startTime = millis();
  while (millis() - startTime < 8000) {
    pox.update();
  }
  currentBPM = pox.getHeartRate();

  // Read Temperature once pulse scan is done
  sensors.requestTemperatures();
  float rawTemp = sensors.getTempCByIndex(0);
  currentTemp = rawTemp + TEMP_CALIBRATION_OFFSET;

  digitalWrite(LED_BLUE, LOW);

  evaluateResults(currentBPM, currentTemp);
}

void evaluateResults(float bpm, float temp) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("B:");
  lcd.print((int)bpm);
  lcd.print(" T:");
  lcd.print(temp, 1);
  lcd.print("C");

  // HUMAN LOGIC GATE: BPM between 40-120, Temp >= 32.0C
  if (bpm > 40 && bpm < 120 && temp >= 32.0) {
    lcd.setCursor(0, 1);
    lcd.print("ACCESS GRANTED");
    digitalWrite(LED_GREEN, HIGH);

    // --- SEND SUCCESS DATA TO BACKEND ---
    sendToCloud("GRANTED", bpm, temp);

    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
    delay(2000);
  } else {
    lcd.setCursor(0, 1);
    lcd.print("ACCESS DENIED");
    digitalWrite(LED_RED, HIGH);

    // --- SEND FAILURE DATA TO BACKEND ---
    sendToCloud("DENIED", bpm, temp);

    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      delay(150);
    }
    delay(2000);
  }

  resetSystem();
}

void sendToCloud(String status, float bpm, float temp) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client; // Plain WiFiClient (no "Secure") for a local XAMPP server
    HTTPClient http;

    String serverPath = String("http://") + BACKEND_HOST + BACKEND_PATH;
    http.begin(client, serverPath);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String httpRequestData = "username=" + currentUser +
                              "&status=" + status +
                              "&bpm=" + String((int)bpm) +
                              "&temp=" + String(temp, 1);

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.print("Backend update success: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Error sending to backend: ");
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  } else {
    Serial.println("WiFi Disconnected. Local server unreachable.");
  }
}

void resetSystem() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
  lcd.clear();
  lcd.print("IDENTIX SYSTEM");
  lcd.setCursor(0, 1);
  lcd.print("READY TO SCAN");

  // Send an IDLE state to the backend so the website resets
  sendToCloud("IDLE", 0, 0);
}
