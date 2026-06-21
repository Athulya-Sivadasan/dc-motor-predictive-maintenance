// ============================================================
//  IoT Industrial Machine Health Monitoring System
//  Athulya Sivadasan — Nest Digital, Kochi — June 2026
//  PATCHED: manual MPU6050/MPU6500 register access
//  (your board's WHO_AM_I = 0x70, i.e. it's an MPU6500-class
//   die on a "GY-521 / MPU6050" board — register map and
//   I2C protocol are compatible, so we talk to it directly
//   instead of using Adafruit_MPU6050's strict 0x68 check.)
// ------------------------------------------------------------
//  ⚠️ SAFE TO SHARE / UPLOAD TO GITHUB:
//  This version uses PLACEHOLDER credentials below.
//  Before uploading this sketch to your actual ESP32, replace
//  WIFI_SSID, WIFI_PASSWORD, TS_CHANNEL_ID, and TS_WRITE_KEY
//  with your real values — but do NOT commit those real values
//  back to a public GitHub repo. (Your original file had your
//  real WiFi password and ThingSpeak key typed in directly,
//  which is why this cleaned copy exists.)
// ------------------------------------------------------------
//  Hardware:
//    ESP32 Dev Board
//    MPU6050/6500 — vibration (I2C: SDA=21, SCL=22)
//    DS18B20  — motor surface temperature (GPIO 4)
//    OLED 0.96" SSD1306 — I2C (same bus as IMU)
//    Buzzer   — GPIO 25 (active buzzer)
//    Green LED — GPIO 26 + 220 ohm resistor (Normal only)
//    White LED — GPIO 27 + 220 ohm resistor (Watch / Warning / Critical)
//    Red LED   — GPIO 14 + 220 ohm resistor (Failure)
//
//  Libraries needed (install via Arduino Library Manager):
//    Adafruit SSD1306
//    Adafruit GFX Library
//    DallasTemperature
//    OneWire
//    ThingSpeak  (by MathWorks)
//    WiFi        (built-in for ESP32)
//  NOTE: Adafruit_MPU6050 / Adafruit_Sensor are NO LONGER
//        needed — we talk to the IMU with raw Wire calls.
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ThingSpeak.h"

// ---- WiFi credentials ----
// ⚠️ Replace these with your own before uploading to your ESP32.
// Never commit your real WiFi password to a public repository.
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- ThingSpeak ----
// ⚠️ Replace these with your own channel ID and write API key.
// Find them at thingspeak.com under your channel's "API Keys" tab.
unsigned long TS_CHANNEL_ID = 0;                       // <-- replace with your channel ID
const char*   TS_WRITE_KEY  = "YOUR_THINGSPEAK_WRITE_KEY"; // <-- replace with your write key

// ---- Pin definitions ----
#define DS18B20_PIN  4
#define BUZZER_PIN   25
#define LED_GREEN    14   // Normal + Watch  → green light
#define LED_WHITE    27   // Warning + Critical → white light
#define LED_RED      26   // Failure → red light
#define OLED_RESET   -1

// ---- OLED setup ----
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

// ---- IMU (MPU6050/MPU6500) register map ----
// Identical between the two chips for the registers we use here.
#define MPU_ADDR        0x68
#define REG_PWR_MGMT_1  0x6B
#define REG_CONFIG      0x1A   // DLPF
#define REG_ACCEL_CONFIG 0x1C  // full-scale range
#define REG_ACCEL_XOUT_H 0x3B  // 6 bytes: X,Y,Z, high+low each
#define REG_WHO_AM_I    0x75

// ±8g range -> sensitivity 4096 LSB/g (matches your original
// MPU6050_RANGE_8_G setting). If you change ACCEL_CONFIG below,
// update ACCEL_SENS_LSB_PER_G to match.
const float ACCEL_SENS_LSB_PER_G = 4096.0;

// ---- DS18B20 setup ----
OneWire           oneWire(DS18B20_PIN);
DallasTemperature tempSensor(&oneWire);

// ---- WiFi client for ThingSpeak ----
WiFiClient client;

// ---- Fault level thresholds ----
const float VIB_WATCH    = 0.30;  // g RMS
const float VIB_WARNING  = 0.60;
const float VIB_CRITICAL = 1.00;
const float VIB_FAILURE  = 1.40;

const float TEMP_WATCH    = 35.0; // degrees C
const float TEMP_WARNING  = 55.0;
const float TEMP_CRITICAL = 65.0;
const float TEMP_FAILURE  = 85.0;

// ---- Timing ----
unsigned long lastThingSpeakUpload = 0;
const unsigned long TS_INTERVAL    = 15000; // ThingSpeak free tier min 15s

// ============================================================
//  Fault level structure
// ============================================================
struct FaultInfo {
  int   level;        // 1=Failure  2=Critical  3=Warning  4=Watch  5=Normal
  int   stars;
  const char* name;
  const char* action;
  const char* ledColor; // which LED should be lit
  bool  blink;          // true = blink at 0.5s interval, false = solid
};

FaultInfo classifyFault(float vibRMS, float temp) {
  int vibLevel  = 5;
  int tempLevel = 5;

  if      (vibRMS >= VIB_FAILURE)  vibLevel = 1;
  else if (vibRMS >= VIB_CRITICAL) vibLevel = 2;
  else if (vibRMS >= VIB_WARNING)  vibLevel = 3;
  else if (vibRMS >= VIB_WATCH)    vibLevel = 4;

  if      (temp >= TEMP_FAILURE)   tempLevel = 1;
  else if (temp >= TEMP_CRITICAL)  tempLevel = 2;
  else if (temp >= TEMP_WARNING)   tempLevel = 3;
  else if (temp >= TEMP_WATCH)     tempLevel = 4;

  return faultInfoForLevel(min(vibLevel, tempLevel));
}

// Maps a raw level number (1-5) to its full FaultInfo struct.
//   5 NORMAL   -> green,  solid
//   4 WATCH    -> green,  blinking
//   3 WARNING  -> white,  solid
//   2 CRITICAL -> red,    solid
//   1 FAILURE  -> red,    blinking
FaultInfo faultInfoForLevel(int level) {
  switch (level) {
    case 1: return {1, 1, "FAILURE",  "SHUT DOWN NOW",     "RED",   true};
    case 2: return {2, 2, "CRITICAL", "Schedule repair",   "RED",   false};
    case 3: return {3, 3, "WARNING",  "Inspect soon",      "WHITE", false};
    case 4: return {4, 4, "WATCH",    "Monitor closely",   "GREEN", true};
    default:return {5, 5, "NORMAL",   "All good",          "GREEN", false};
  }
}

// ============================================================
//  LED control — only one LED active at a time
//  Solid states (Normal/Warning/Critical) just hold steady.
//  Blink states (Watch/Failure) toggle on/off every 0.5s using
//  millis(), so this never blocks the rest of loop().
// ============================================================
unsigned long lastLedToggleTime = 0;
bool          ledBlinkState     = false; // current on/off phase for blinking LEDs
const unsigned long LED_BLINK_INTERVAL = 500; // ms

void updateLEDs(FaultInfo fault) {
  // Turn everything off first, then light only the active color
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_WHITE, LOW);
  digitalWrite(LED_RED,   LOW);

  if (!fault.blink) {
    // Solid: just hold the right LED HIGH
    if      (strcmp(fault.ledColor, "GREEN") == 0) digitalWrite(LED_GREEN, HIGH);
    else if (strcmp(fault.ledColor, "WHITE") == 0) digitalWrite(LED_WHITE, HIGH);
    else if (strcmp(fault.ledColor, "RED")   == 0) digitalWrite(LED_RED,   HIGH);
    return;
  }

  // Blinking: toggle phase every LED_BLINK_INTERVAL ms
  unsigned long now = millis();
  if (now - lastLedToggleTime >= LED_BLINK_INTERVAL) {
    ledBlinkState     = !ledBlinkState;
    lastLedToggleTime = now;
  }

  if (ledBlinkState) {
    if      (strcmp(fault.ledColor, "GREEN") == 0) digitalWrite(LED_GREEN, HIGH);
    else if (strcmp(fault.ledColor, "RED")   == 0) digitalWrite(LED_RED,   HIGH);
  }
  // else: stays LOW (already set above) for the "off" phase
}

// ============================================================
//  Low-level IMU register helpers
// ============================================================
bool imuWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return (Wire.endTransmission() == 0);
}

uint8_t imuReadRegister(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false); // keep bus held for repeated start
  Wire.requestFrom((int)MPU_ADDR, 1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0;
}

// Reads 6 bytes starting at ACCEL_XOUT_H and fills ax,ay,az (raw int16)
bool imuReadAccelRaw(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t bytesReceived = Wire.requestFrom((int)MPU_ADDR, 6);
  if (bytesReceived < 6) return false;

  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
  return true;
}

// Initializes the IMU manually — works for MPU6050 (WHO_AM_I 0x68)
// and MPU6500 (WHO_AM_I 0x70), since the register map used here
// is identical between them.
bool initIMU() {
  uint8_t whoAmI = imuReadRegister(REG_WHO_AM_I);
  Serial.print("IMU WHO_AM_I = 0x");
  Serial.println(whoAmI, HEX);

  if (whoAmI != 0x68 && whoAmI != 0x70 && whoAmI != 0x71 && whoAmI != 0x73) {
    // Not a recognized MPU6050/6500/9250-family value at all —
    // likely wrong wiring/address rather than a clone-chip issue.
    Serial.println("Unrecognized IMU WHO_AM_I — check wiring/address.");
    return false;
  }

  // Wake up the chip (it boots into sleep mode by default)
  if (!imuWriteRegister(REG_PWR_MGMT_1, 0x00)) return false;
  delay(50);

  // ±8g full-scale range -> matches ACCEL_SENS_LSB_PER_G above
  if (!imuWriteRegister(REG_ACCEL_CONFIG, 0x10)) return false;

  // Digital low-pass filter, ~21Hz bandwidth (matches your
  // original MPU6050_BAND_21_HZ setting)
  if (!imuWriteRegister(REG_CONFIG, 0x04)) return false;

  delay(50);
  return true;
}

// ============================================================
//  Calculate vibration RMS from IMU
//  Takes 50 samples and returns magnitude RMS in g
// ============================================================
float readVibrationRMS() {
  const int SAMPLES = 50;
  float     sumSq   = 0;
  int       validSamples = 0;

  for (int i = 0; i < SAMPLES; i++) {
    int16_t rawX, rawY, rawZ;
    if (imuReadAccelRaw(rawX, rawY, rawZ)) {
      float ax = rawX / ACCEL_SENS_LSB_PER_G;
      float ay = rawY / ACCEL_SENS_LSB_PER_G;
      float az = rawZ / ACCEL_SENS_LSB_PER_G;

      float magnitude = sqrt(ax*ax + ay*ay + az*az);
      magnitude = abs(magnitude - 1.0);
      sumSq += magnitude * magnitude;
      validSamples++;
    }
    delay(2);
  }

  if (validSamples == 0) return 0.0; // avoid divide-by-zero if IMU dropped off bus
  return sqrt(sumSq / validSamples);
}

// ============================================================
//  OLED display — shows readings, stars, LED colour, action
// ============================================================
void updateDisplay(float vibRMS, float temp, FaultInfo fault) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Machine Monitor");

  display.setCursor(0, 12);
  display.print("Vib: ");
  display.print(vibRMS, 2);
  display.print(" g");

  display.setCursor(0, 22);
  display.print("Tmp: ");
  display.print(temp, 1);
  display.print(" C");

  display.setCursor(0, 34);
  display.print(fault.name);
  display.print(" [");
  display.print(fault.ledColor);
  display.print("]");

  display.setCursor(0, 44);
  for (int i = 0; i < fault.stars; i++)  display.print("*");
  for (int i = fault.stars; i < 5; i++)  display.print("-");

  display.setCursor(0, 54);
  display.print(fault.action);

  display.display();
}

// ============================================================
//  Buzzer pattern
//    NORMAL / WATCH (green, solid or blinking) -> silent
//    WARNING (white, solid)                    -> single beep, then quiet
//    CRITICAL (red, solid)                     -> beep, 0.5s pause, repeat
//    FAILURE (red, blinking)                   -> continuous steady tone
//  Uses millis() so it never blocks sensor reads or LED blinking.
// ============================================================
unsigned long lastBuzzerToggleTime = 0;
bool          buzzerOnPhase        = false;
const unsigned long BUZZER_INTERVAL = 500; // ms, beep/pause duration for Critical

// Tracks whether we just switched INTO Warning, so its single
// beep fires once per state-entry rather than every loop pass.
int  lastFaultLevelForBuzzer = -1;
bool warningBeepDone         = false;

void soundBuzzer(int faultLevel) {
  // Reset the "single beep" tracker whenever the fault level changes
  if (faultLevel != lastFaultLevelForBuzzer) {
    warningBeepDone        = false;
    lastFaultLevelForBuzzer = faultLevel;
  }

  switch (faultLevel) {
    case 5: // NORMAL — silent
    case 4: // WATCH — silent
      digitalWrite(BUZZER_PIN, LOW);
      break;

    case 3: // WARNING — exactly one beep per state-entry, then quiet
      if (!warningBeepDone) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150); // short single beep; brief blocking ok, runs once
        digitalWrite(BUZZER_PIN, LOW);
        warningBeepDone = true;
      }
      break;

    case 2: { // CRITICAL — beep, 0.5s pause, beep, 0.5s pause, repeating
      unsigned long now = millis();
      if (now - lastBuzzerToggleTime >= BUZZER_INTERVAL) {
        buzzerOnPhase        = !buzzerOnPhase;
        lastBuzzerToggleTime = now;
      }
      digitalWrite(BUZZER_PIN, buzzerOnPhase ? HIGH : LOW);
      break;
    }

    case 1: // FAILURE — continuous tone, no pulsing
      digitalWrite(BUZZER_PIN, HIGH);
      break;
  }
}

// ============================================================
//  Upload to ThingSpeak
// ============================================================
void uploadToThingSpeak(float vibRMS, float temp, int faultLevel) {
  ThingSpeak.setField(1, vibRMS);
  ThingSpeak.setField(2, temp);
  ThingSpeak.setField(3, faultLevel);

  int result = ThingSpeak.writeFields(TS_CHANNEL_ID, TS_WRITE_KEY);

  if (result == 200) {
    Serial.println("[ThingSpeak] Upload OK");
  } else {
    Serial.print("[ThingSpeak] Error: ");
    Serial.println(result);
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(21, 22);
  Wire.setClock(100000); // 100kHz — more reliable with two devices/long wires

  Serial.println("Scanning I2C...");
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found device at 0x");
      Serial.println(address, HEX);
    }
  }

  // Buzzer and LED pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_WHITE,  OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_WHITE,  LOW);
  digitalWrite(LED_RED,    LOW);

  // Startup LED test
  Serial.println("LED test...");
  digitalWrite(LED_GREEN, HIGH); delay(400); digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_WHITE, HIGH); delay(400); digitalWrite(LED_WHITE, LOW);
  digitalWrite(LED_RED,   HIGH); delay(400); digitalWrite(LED_RED,   LOW);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Starting up...");
  display.display();

  // IMU init (manual — works for MPU6050 *and* MPU6500-class clones)
  Serial.println("Trying IMU...");
  if (!initIMU()) {
    Serial.println("IMU not found / failed to init");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("IMU INIT FAILED");
    display.display();
    while (true);
  }
  Serial.println("IMU ready");

  // DS18B20 init
  tempSensor.begin();
  Serial.println("DS18B20 ready");

  // WiFi connect
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED — running offline");
  }

  ThingSpeak.begin(client);
  delay(500);
  Serial.println("System ready");
}

// ============================================================
//  MAIN LOOP
//  Sensor reads (vibration + temp) are relatively slow, so they
//  run on their own schedule. LED blinking and buzzer beeping
//  need to be checked frequently (every few ms) to look smooth
//  at the 0.5s interval, so they're updated every pass through
//  loop() using the most recently computed fault state.
// ============================================================
FaultInfo currentFault = {5, 5, "NORMAL", "All good", "GREEN", false};
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 0; // 0 = read again immediately after each finishes

void loop() {
  unsigned long now = millis();

  // --- Slow path: sensor read + classify + display + upload ---
  // (readVibrationRMS()/requestTemperatures() are themselves the
  // pacing here, so SENSOR_READ_INTERVAL can stay 0 — adjust if
  // you want extra spacing between full read cycles.)
  if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    float vibRMS = readVibrationRMS();
    tempSensor.requestTemperatures();
    float temp = tempSensor.getTempCByIndex(0);

    currentFault = classifyFault(vibRMS, temp);

    Serial.printf("Vib: %.3f g | Temp: %.1f C | %s | LED: %s\n",
                  vibRMS, temp, currentFault.name, currentFault.ledColor);

    updateDisplay(vibRMS, temp, currentFault);

    if (WiFi.status() == WL_CONNECTED) {
      if (now - lastThingSpeakUpload >= TS_INTERVAL) {
        uploadToThingSpeak(vibRMS, temp, currentFault.level);
        lastThingSpeakUpload = now;
      }
    }

    lastSensorReadTime = now;
  }

  // --- Fast path: LED blink + buzzer beep, checked every pass ---
  updateLEDs(currentFault);
  soundBuzzer(currentFault.level);
}
