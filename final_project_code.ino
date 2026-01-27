/*
  PAR sensor / Grow-light controller — Verification TELEMETRY ONLY + DLI (mol/m^2/day)
  - Verification does NOT change relay or control flow (info-only).
  - Savings start at 0 and increase only while lamp is OFF.
  - ThingSpeak:
      field1: 0.0=off, 1.0=confirmed on, 0.5=fault (telemetry only)
      field6: growlight DLI (mol/m^2/day)
      field7: avg PPFD (µmol m^-2 s^-1)
      field8: total DLI (mol m^2/day)
*/

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ===================== HARDWARE =====================
Adafruit_ADS1115 ads;

// Relay pin (physical control of the grow light)
const int RELAY_PIN = 16;            // change if your hardware requires another GPIO
const bool RELAY_ACTIVE_HIGH = true; // true if setting HIGH turns the relay (lamp) ON

// ===================== WIFI =====================
const char* ssid = "agrotech";
const char* password = "1Afuna2gezer";

// ===================== MQTT =====================
const char* mqtt_server = "192.168.0.102";
const int   mqtt_port = 1883;
const char* mqtt_user = "mqtt-user";
const char* mqtt_password = "1234";
const char* mqtt_topic = "/greenhouse/sockets/socket1";

WiFiClient espClient;
PubSubClient client(espClient);

// ===================== TIME =====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;     // UTC+2
const int daylightOffset_sec = 3600; // DST offset if you want (kept from original)

// ===================== THINGSPEAK =====================
const char* TS_HOST = "api.thingspeak.com";
const char* TS_WRITE_KEY = "JSFT7BA0D9DANFE4";
const unsigned long THINGSPEAK_INTERVAL_MS = 20000UL;
unsigned long lastThingSpeakMillis = 0;

// ===================== LIGHT CONTROL =====================
bool lightState = false;         // commanded state (true => command ON) — reflects relay output
unsigned long lastToggleMillis = 0;
unsigned long lastChangeMillis = 0;

// Verified lamp states (telemetry only)
bool lampConfirmed = false; // true when sensor confirms lamp is actually on (info)
bool lampFault = false;     // true when verification failed (info)

// Verification parameters (ON-only, telemetry only)
const float LAMP_SELF_PPFD = 10.0f;         // µmol contributed by lamp to sensor (small local bleed)
const unsigned long LAMP_VERIFY_TIME_MS = 5000UL;   // wait time for ON verification (non-blocking)
const float LAMP_VERIFY_MIN_DELTA = 3.0f;   // minimum expected raw change (µmol) to assume lamp turned ON
const int MAX_VERIFY_ATTEMPTS = 2;          // number of retry attempts for ON verification

float ppfdAtToggle = 0.0f;              // raw reading at time of ON command for verification
unsigned long lampVerifyStart = 0;      // timestamp when verification waiting period began
int verifyAttempts = 0;

// ===================== SENSOR =====================
const float SENSOR_SENSITIVITY_V_PER_UMOL = 0.00000768f; // V per µmol (as in original)
const float GROWLIGHT_PPFD = 200.0f; // used only for growlight PPFD integration (µmol m^-2 s^-1 estimate)

// Control thresholds
const float PPFD_THRESHOLD = 200.0f;
const float PPFD_HYSTERESIS = 30.0f;
const unsigned long STEADY_TIME_MS = 10000UL;
const unsigned long TOGGLE_LOCK_MS = 30000UL;

unsigned long belowTimerStart = 0;
unsigned long aboveTimerStart = 0;

// ===================== POWER =====================
const float LAMP_POWER_KW = 0.065f;   // 65 W lamp
const float PRICE_PER_KWH = 0.64f;    // in shekels

float energyToday = 0.0f;
float energyWeek = 0.0f;
float energyMonth = 0.0f;

float savingsToday = 0.0f;   // shekels saved by turning off the lamp (only increases while lamp is OFF)
float savingsWeek = 0.0f;
float savingsMonth = 0.0f;

unsigned long lightOnTodaySeconds = 0;
unsigned long lastEnergyUpdate = 0;

// ===================== PPFD BUFFER =====================
const int PPFD_BUF_SIZE = 20;
float ppfdBuf[PPFD_BUF_SIZE];
int ppfdIdx = 0;
int ppfdCount = 0;

float dailyMaxPPFD = 0;

// ===================== PPFD INTEGRATION (µmol) =====================
// Accumulators store µmol/m^2 integrated over the day (µmol m^-2)
double dailyAmbientPPFD_umol = 0;    // accumulated ambient µmol (from sensor)
double dailyGrowlightPPFD_umol = 0;  // accumulated growlight µmol (estimated when relay ON)

// ===================== DATE TRACKING =====================
int lastDay = -1, lastWeek = -1, lastMonth = -1;

// ===================== HELPERS =====================
void setRelay(bool on) {
  int level = RELAY_ACTIVE_HIGH ? (on ? HIGH : LOW) : (on ? LOW : HIGH);
  digitalWrite(RELAY_PIN, level);
  Serial.print("Relay -> ");
  Serial.println(on ? "ON" : "OFF");
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm t;
  while (!getLocalTime(&t)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2s");
      delay(2000);
    }
  }
}

float ppfdAvg20s() {
  float s = 0;
  for (int i = 0; i < ppfdCount; i++) s += ppfdBuf[i];
  return ppfdCount ? s / ppfdCount : 0;
}

void publishToThingSpeak(float avgPPFD) {
  unsigned long now = millis();
  if (now - lastThingSpeakMillis < THINGSPEAK_INTERVAL_MS) return;
  lastThingSpeakMillis = now;

  float weeklyCost = energyWeek * PRICE_PER_KWH;
  float minutesOn = lightOnTodaySeconds / 60UL;

  // Convert integrated µmol to mol/m^2 (DLI)
  double growlightDaily_mol = dailyGrowlightPPFD_umol / 1e6; // mol/m^2 (DLI from growlight)
  double totalDailyDLI_mol = (dailyAmbientPPFD_umol + dailyGrowlightPPFD_umol) / 1e6; // mol/m^2/day

  // field1: lamp state: 0 = off, 1 = confirmed on, 0.5 = fault (telemetry only)
  float tsLightState;
  if (lampFault) tsLightState = 0.5f;
  else if (lampConfirmed) tsLightState = 1.0f;
  else tsLightState = 0.0f;

  String body = "api_key=" + String(TS_WRITE_KEY);
  body += "&field1=" + String(tsLightState, 1);
  body += "&field2=" + String((unsigned long)minutesOn);
  body += "&field3=" + String(energyWeek, 4);
  body += "&field4=" + String(weeklyCost, 2);
  // field5: savings (weekly) in shekels
  body += "&field5=" + String(savingsWeek, 2);
  // field6: growlight contribution in mol/m^2 (daily)
  body += "&field6=" + String(growlightDaily_mol, 4);
  // field7: average PPFD (µmol m^-2 s^-1)
  body += "&field7=" + String(avgPPFD, 2);
  // field8: total daily DLI in mol/m^2/day
  body += "&field8=" + String(totalDailyDLI_mol, 4);

  Serial.print("ThingSpeak -> field1 (light state): ");
  Serial.println(tsLightState, 1);
  Serial.print("ThingSpeak -> field5 (weekly savings): ");
  Serial.println(savingsWeek, 2);
  Serial.print("ThingSpeak -> field6 (growlight DLI mol/m2/day): ");
  Serial.println(growlightDaily_mol, 4);
  Serial.print("ThingSpeak -> field8 (total DLI mol/m2/day): ");
  Serial.println(totalDailyDLI_mol, 4);

  WiFiClient http;
  if (!http.connect(TS_HOST, 80)) {
    Serial.println("ThingSpeak connect failed");
    return;
  }

  http.print(
    "POST /update HTTP/1.1\r\n"
    "Host: api.thingspeak.com\r\n"
    "Connection: close\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: " + String(body.length()) + "\r\n\r\n" +
    body
  );
  delay(100);
  http.stop();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.println("=== PAR Sensor / Grow-light Controller (Verification = telemetry only) ===");

  pinMode(RELAY_PIN, OUTPUT);
  // ensure relay state and internal state are consistent at startup
  setRelay(false);
  lightState = false;
  lampConfirmed = false;
  lampFault = false;

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  reconnect();

  Wire.begin(21, 22);
  if (!ads.begin(0x48)) {
    Serial.println("Failed to initialize ADS. Check wiring.");
    while (1) delay(1000);
  }
  ads.setGain(GAIN_SIXTEEN);

  lastEnergyUpdate = millis();
  lastChangeMillis = millis();

  Serial.println("Setup complete.");
}

// ===================== LOOP =====================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();
  struct tm timeinfo;
  bool synced = getLocalTime(&timeinfo);

  // Daily/weekly/monthly resets (based on local time if available)
  if (synced) {
    if (timeinfo.tm_mday != lastDay) {
      energyToday = 0.0f;
      lightOnTodaySeconds = 0;
      dailyAmbientPPFD_umol = 0;
      dailyGrowlightPPFD_umol = 0;
      dailyMaxPPFD = 0;
      savingsToday = 0.0f;
      lastDay = timeinfo.tm_mday;
      Serial.println("New day reset.");
    }
    int w = timeinfo.tm_yday / 7;
    if (w != lastWeek) {
      energyWeek = 0.0f;
      savingsWeek = 0.0f;
      lastWeek = w;
      Serial.println("New week reset.");
    }
    if (timeinfo.tm_mon != lastMonth) {
      energyMonth = 0.0f;
      savingsMonth = 0.0f;
      lastMonth = timeinfo.tm_mon;
      Serial.println("New month reset.");
    }
  }

  bool isNight = synced && ((timeinfo.tm_hour >= 21) || (timeinfo.tm_hour < 5));

  // Read sensor (raw)
  int16_t adc = ads.readADC_SingleEnded(0);
  float voltage = adc * 0.0000078125f; // ADS1115 LSB
  float ppfd_raw = voltage / SENSOR_SENSITIVITY_V_PER_UMOL; // raw measured µmol (includes lamp bleed)

  // --- Non-blocking ON verification monitoring (TELEMETRY ONLY) ---
  if (lampVerifyStart > 0) {
    if (now - lampVerifyStart >= LAMP_VERIFY_TIME_MS) {
      // evaluate whether expected rise occurred
      float delta = ppfd_raw - ppfdAtToggle;
      Serial.print("Verify ON -> delta raw: ");
      Serial.println(delta, 2);
      if (delta >= LAMP_VERIFY_MIN_DELTA) {
        // verification success (telemetry only)
        lampConfirmed = true;
        lampFault = false;
        lampVerifyStart = 0;
        verifyAttempts = 0;
        Serial.println("✅ Lamp verified ON (sensor delta sufficient) — telemetry only.");
        lastChangeMillis = now;
      } else {
        // verification attempt failed: schedule retry or final failure (non-blocking, telemetry only)
        if (verifyAttempts < MAX_VERIFY_ATTEMPTS) {
          verifyAttempts++;
          Serial.print("⚠️ Verify ON failed, scheduling retry ");
          Serial.print(verifyAttempts);
          Serial.println(" (telemetry-only; no relay toggles).");
          // Reset baseline to current reading and start a new verification period
          ppfdAtToggle = ppfd_raw;
          lampVerifyStart = now; // start next verification window
        } else {
          // final failure -> mark fault (telemetry only) but do NOT change relay or control
          lampFault = true;
          lampConfirmed = false;
          lampVerifyStart = 0;
          verifyAttempts = 0;
          Serial.println("❌ Lamp verification FAILED after retries — marked FAULT (telemetry only). Relay NOT changed.");
        }
      }
    }
  }

  // Corrected ppfd for ambient/control: **based on commanded relay (lightState)**, not on verification
  // This ensures verification does NOT affect control.
  float ppfd_corrected = ppfd_raw;
  if (lightState) {
    // If we commanded lamp ON, subtract nominal lamp self contribution to get ambient estimate
    ppfd_corrected = ppfd_raw - LAMP_SELF_PPFD;
    if (ppfd_corrected < 0) ppfd_corrected = 0;
  }

  // Buffer & stats use ppfd_corrected (ambient estimate used for control & averages)
  ppfdBuf[ppfdIdx] = ppfd_corrected;
  ppfdIdx = (ppfdIdx + 1) % PPFD_BUF_SIZE;
  if (ppfdCount < PPFD_BUF_SIZE) ppfdCount++;

  dailyMaxPPFD = max(dailyMaxPPFD, ppfd_corrected);

  float avg = ppfdAvg20s();

  float low = PPFD_THRESHOLD - PPFD_HYSTERESIS;
  float high = PPFD_THRESHOLD + PPFD_HYSTERESIS;

  bool wantOn = false, wantOff = false;

  if (isNight) wantOff = true;
  else {
    if (ppfd_corrected < low) {
      if (!belowTimerStart) belowTimerStart = now;
      if (now - belowTimerStart > STEADY_TIME_MS) wantOn = true;
    } else belowTimerStart = 0;

    if (ppfd_corrected > high) {
      if (!aboveTimerStart) aboveTimerStart = now;
      if (now - aboveTimerStart > STEADY_TIME_MS) wantOff = true;
    } else aboveTimerStart = 0;
  }

  // SERIAL STATUS
  Serial.print("PPFD raw: ");
  Serial.print(ppfd_raw, 2);
  Serial.print(" | corrected(ambient-est): ");
  Serial.print(ppfd_corrected, 2);
  Serial.print(" | avg: ");
  Serial.print(avg, 2);
  Serial.print(" | commanded(lightState): ");
  Serial.print(lightState ? "ON" : "OFF");
  Serial.print(" | confirmed: ");
  Serial.print(lampConfirmed ? "YES" : "NO");
  Serial.print(" | fault: ");
  Serial.println(lampFault ? "YES" : "NO");

  // Toggle logic with lock (verification NEVER suppresses ON or OFF)
  if ((wantOn && !lightState) || (wantOff && lightState)) {
    if (now - lastToggleMillis > TOGGLE_LOCK_MS) {
      bool previousLightState = lightState;
      lightState = wantOn; // update internal commanded state
      setRelay(lightState); // physically toggle relay

      // Publish MQTT command in parallel (keep for external sockets/logs)
      bool pubOk = client.publish(mqtt_topic, lightState ? "1" : "0");
      Serial.print("MQTT publish to ");
      Serial.print(mqtt_topic);
      Serial.print(" -> ");
      Serial.println(lightState ? "1" : "0");
      Serial.print("MQTT publish success: ");
      Serial.println(pubOk ? "YES" : "NO");

      lastToggleMillis = now;
      lastChangeMillis = now;
      belowTimerStart = aboveTimerStart = 0;

      if (lightState && !previousLightState) {
        // We commanded ON: start verification (telemetry only)
        ppfdAtToggle = ppfd_raw;
        lampVerifyStart = now;
        verifyAttempts = 0;
        // clear the previous telemetry flags to allow fresh detection
        lampFault = false;
        lampConfirmed = false;
        Serial.println("Lamp ON command issued -> starting verification (telemetry only).");
        Serial.print("  baseline raw PPFD: ");
        Serial.println(ppfdAtToggle, 2);
      } else if (!lightState && previousLightState) {
        // We commanded OFF: stop any pending verification (telemetry only)
        lampVerifyStart = 0;
        verifyAttempts = 0;
        // Do NOT clear lampFault here (we keep last known telemetry).  If you want to clear fault on OFF, uncomment the next line:
        // lampFault = false;
        lampConfirmed = false;
        Serial.println("Lamp OFF command issued -> verification cancelled (telemetry only).");
      }

    } else {
      Serial.println("Toggle request ignored due to toggle lock.");
    }
  }

  // Energy & PPFD integration (based on commanded lightState — control is independent of verification)
  unsigned long dt = now - lastEnergyUpdate;
  if (dt > 0) {
    float hours = dt / 3600000.0f;
    unsigned long secs = dt / 1000UL;

    if (lightState) {
      // Lamp commanded ON -> count energy consumption & estimated growlight PPFD
      float e = LAMP_POWER_KW * hours;
      energyToday += e;
      energyWeek += e;
      energyMonth += e;
      lightOnTodaySeconds += secs;
      // Estimate growlight contribution using GROWLIGHT_PPFD constant * seconds (µmol)
      dailyGrowlightPPFD_umol += (double)GROWLIGHT_PPFD * (double)secs; // µmol integrated
    } else {
      // Lamp commanded OFF -> accumulate savings (avoided cost)
      float avoided_energy = LAMP_POWER_KW * hours; // kWh that would have been used if lamp ON
      float avoided_cost = avoided_energy * PRICE_PER_KWH;
      savingsToday += avoided_cost;
      savingsWeek  += avoided_cost;
      savingsMonth += avoided_cost;
    }

    // ambient integration (use ppfd_corrected which is the ambient estimate used for control)
    dailyAmbientPPFD_umol += (double)ppfd_corrected * (double)secs;

    lastEnergyUpdate = now;
  }

  publishToThingSpeak(avg);

  delay(1000); // keep a 1s cadence — verification & control are non-blocking
}
