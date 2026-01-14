/*
  Final PAR sensor / grow-light controller
  - ADS1115 reads PPFD sensor
  - Local control with hysteresis, steady-time, and lockout
  - MQTT publishes state changes to your broker
  - ThingSpeak HTTP uploads (free-tier safe) every 20s
  - Fields mapping (ThingSpeak):
      field1:  Grow light state (0/1)
      field2:  Minutes ON today
      field3:  Daily electricity usage (kWh)
      field4:  Daily electricity cost (₪)
      field5:  Weekly electricity usage (kWh)
      field6:  Weekly electricity cost (₪)
      field7:  PPFD avg last 20s
      field8:  Max PPFD today
  Observations:
    - This version intentionally DOES NOT use ThingSpeak to receive remote commands
      (to avoid conflicts with telemetry fields). Use MQTT or a separate TS
      command feed to do remote overrides.
*/

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ===================== HARDWARE / LIBS =====================
Adafruit_ADS1115 ads;             // ADS1115 ADC module

// ===================== WIFI =====================
const char* ssid     = "agrotech";
const char* password = "1Afuna2gezer";

// ===================== MQTT (local) =====================
const char* mqtt_server   = "192.168.0.102";
const int   mqtt_port     = 1883;
const char* mqtt_user     = "mqtt-user";
const char* mqtt_password = "1234";
const char* mqtt_topic    = "/greenhouse/sockets/socket1";

WiFiClient espClient;
PubSubClient client(espClient);

// ===================== NTP / TIME (Israel) =====================
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;     // UTC+2
const int   daylightOffset_sec = 3600;

// ===================== THINGSPEAK (HTTP) =====================
const char* TS_HOST = "api.thingspeak.com";
const char* TS_WRITE_KEY = "JSFT7BA0D9DANFE4";   // replace if you regenerate
const long  TS_CHANNEL_ID = 3223345;             // informational only
// Safety: free-tier minimum is 15s -> we use 20s to be conservative
const unsigned long THINGSPEAK_INTERVAL_MS = 20000UL;
unsigned long lastThingSpeakMillis = 0;

// ===================== LIGHT CONTROL =====================
bool lightState = false;            // current relay / light state
unsigned long lastChangeMillis = 0; // when state last changed (for timeInCurrent)

// ===================== SENSOR / PPFD =====================
// scaling: user provided constant (voltage per umol)
const float SENSOR_SENSITIVITY_V_PER_UMOL = 0.00000768f;

// Control parameters (fine tune as needed)
const float PPFD_THRESHOLD  = 200.0f;   // target PPFD
const float PPFD_HYSTERESIS = 30.0f;    // hysteresis around threshold

// Timers for debouncing and lockout
const unsigned long STEADY_TIME_MS = 10000UL;    // require 10s stable before toggle
const unsigned long TOGGLE_LOCK_MS = 30000UL;    // 30s lockout after toggle

unsigned long lastToggleMillis = 0;
unsigned long belowTimerStart = 0;
unsigned long aboveTimerStart = 0;

// ===================== POWER & COST =====================
// Lamp average power in kW (65 W -> 0.065 kW)
const float LAMP_POWER_KW = 0.065f;
// Israel electricity price (₪ per kWh)
const float PRICE_PER_KWH = 0.64f;

// Energy counters (kWh)
float energyToday = 0.0f;
float energyWeek  = 0.0f;
float energyMonth = 0.0f;
float energyTotal = 0.0f;

// last timestamp used for energy accumulation (millis())
unsigned long lastEnergyUpdate = 0;
// keep track of day/week/month for resets (tm fields)
int lastDay = -1, lastWeek = -1, lastMonth = -1;

// Track how many seconds the light has been ON today
unsigned long lightOnTodaySeconds = 0;

// ===================== PPFD ROLLING BUFFER =====================
// We sample roughly once per loop (~1s). Buffer size 20 -> ~20s average.
const int PPFD_BUF_SIZE = 20;
float ppfdBuf[PPFD_BUF_SIZE];
int ppfdBufIndex = 0;
int ppfdBufCount = 0;

// daily maximum PPFD (resets at midnight)
float dailyMaxPPFD = 0.0f;

// ===================== SETUP HELPERS =====================

void setup_wifi() {
  Serial.print("WiFi: connecting");
  WiFi.begin(ssid, password);
  // Wait until connected
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("WiFi connected, IP=");
  Serial.println(WiFi.localIP());

  // Configure and sync time (blocking until synced)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm t;
  // Wait for NTP sync to succeed before continuing (ensures correct resets)
  while (!getLocalTime(&t)) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  Serial.println("Time synced via NTP");
}

void reconnect() {
  // MQTT reconnect with simple loop; keep trying
  while (!client.connected()) {
    Serial.print("MQTT: connecting...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

// compute average of the buffer
float ppfdBufferAvg() {
  if (ppfdBufCount == 0) return 0.0f;
  float s = 0.0f;
  for (int i = 0; i < ppfdBufCount; ++i) s += ppfdBuf[i];
  return s / (float)ppfdBufCount;
}

// compute max of the buffer
float ppfdBufferMax() {
  if (ppfdBufCount == 0) return 0.0f;
  float m = ppfdBuf[0];
  for (int i = 1; i < ppfdBufCount; ++i) if (ppfdBuf[i] > m) m = ppfdBuf[i];
  return m;
}

/*
  publishToThingSpeak
  - Hard-locked: returns immediately if last upload < THINGSPEAK_INTERVAL_MS
  - Immediately updates lastThingSpeakMillis before sending to avoid double-post races
  - Sends the requested 8 fields (see heading comments)
*/
void publishToThingSpeak(float ppfdAvg20s, float dailyMaxPPFD_val, unsigned long timeInCurrent_s) {
  unsigned long now = millis();

  // Enforce hard rate-limit (safe for free tier)
  if (now - lastThingSpeakMillis < THINGSPEAK_INTERVAL_MS) {
    // Not yet time for next ThingSpeak upload
    return;
  }

  // Lock immediately to avoid multiple threads/loops posting concurrently
  lastThingSpeakMillis = now;

  // Build payload
  float dailyCost   = energyToday * PRICE_PER_KWH;
  float weeklyCost  = energyWeek  * PRICE_PER_KWH;
  // monthly cost kept internally only (not sent per current mapping)
  float monthlyCost = energyMonth * PRICE_PER_KWH;

  unsigned long minutesOnToday = lightOnTodaySeconds / 60UL;

  String body = "api_key=" + String(TS_WRITE_KEY);
  body += "&field1=" + String(lightState ? 1 : 0);         // grow light state
  body += "&field2=" + String(minutesOnToday);             // minutes ON today
  body += "&field3=" + String(energyToday, 4);             // daily kWh
  body += "&field4=" + String(dailyCost, 2);               // daily cost (₪)
  body += "&field5=" + String(energyWeek, 4);              // weekly kWh
  body += "&field6=" + String(weeklyCost, 2);              // weekly cost (₪)
  body += "&field7=" + String(ppfdAvg20s, 2);              // ppfd average last 20s
  body += "&field8=" + String(dailyMaxPPFD_val, 2);        // max ppfd today

  // HTTP POST request
  WiFiClient http;
  if (!http.connect(TS_HOST, 80)) {
    Serial.println("TS: connect failed (publish)");
    return;
  }

  // Compose request (include content-length)
  String req = String("POST /update HTTP/1.1\r\n") +
               "Host: " + TS_HOST + "\r\n" +
               "Connection: close\r\n" +
               "Content-Type: application/x-www-form-urlencoded\r\n" +
               "Content-Length: " + String(body.length()) + "\r\n\r\n" +
               body;

  http.print(req);

  // Optional: read response (ThingSpeak returns the entry id or "0")
  unsigned long start = millis();
  while (http.available() == 0) {
    if (millis() - start > 3000) {
      Serial.println("TS: no response (timeout)");
      http.stop();
      return;
    }
    delay(5);
  }

  // Drain response (for debug you can parse first non-header line)
  while (http.available()) {
    String line = http.readStringUntil('\n');
    //Serial.println(line); // uncomment for verbose debugging
  }
  http.stop();

  Serial.println("ThingSpeak: upload done");
}

void setup() {
  Serial.begin(115200);
  delay(10);

  // Connect WiFi and sync time (NTP)
  setup_wifi();

  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  reconnect();

  // I2C pins for ESP32: SDA=21, SCL=22 (Wire.begin(SDA, SCL))
  Wire.begin(21, 22);

  // Initialize ADS1115 at default address 0x48
  if (!ads.begin(0x48)) {
    Serial.println("ERROR: ADS1115 not found. Check wiring.");
    // If ADC is critical, halt here so you notice the problem
    while (1) { delay(1000); }
  }
  // Use maximum gain for small voltages (check if this matches your sensor)
  ads.setGain(GAIN_SIXTEEN);

  // Initialize timers
  lastEnergyUpdate = millis();
  lastThingSpeakMillis = 0;  // allow immediate first upload
  lastChangeMillis = millis();

  // buffer init
  ppfdBufCount = 0;
  ppfdBufIndex = 0;

  dailyMaxPPFD = 0.0f;
  lightOnTodaySeconds = 0;

  Serial.println("SYSTEM READY");
}

void loop() {
  // Ensure MQTT remains connected
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  // Get the local time structure; many operations (resets) use the fields
  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  // If NTP fails temporarily, timeSynced will be false. We still continue,
  // but daily/week/month resets will wait until NTP is available.

  int hour = timeSynced ? timeinfo.tm_hour : 0;
  int minute = timeSynced ? timeinfo.tm_min : 0;
  int nowMin = hour * 60 + minute;

  // ===================== Resets by date/week/month =====================
  if (timeSynced) {
    if (timeinfo.tm_mday != lastDay) {
      // New day: reset daily counters
      energyToday = 0.0f;
      lightOnTodaySeconds = 0UL;
      dailyMaxPPFD = 0.0f;   // reset daily maximum
      lastDay = timeinfo.tm_mday;
    }
    // simple weekly bucket using day-of-year /7 (works for monitoring)
    int weekBucket = timeinfo.tm_yday / 7;
    if (weekBucket != lastWeek) {
      energyWeek = 0.0f;
      lastWeek = weekBucket;
    }
    if (timeinfo.tm_mon != lastMonth) {
      energyMonth = 0.0f;
      lastMonth = timeinfo.tm_mon;
    }
  }

  // ===================== NIGHT DETECTION =====================
  // If time is not synced, we do NOT force-night-off to avoid accidental shutoff.
  bool isNight = timeSynced && ( (nowMin >= 21*60) || (nowMin < 5*60) );

  // ===================== SENSOR READ =====================
  // Read ADC (single-ended channel 0)
  int16_t adc0 = ads.readADC_SingleEnded(0);
  // ADS1115 LSB scaling for GAIN_SIXTEEN -> 0.0000078125 V per count (user provided)
  float voltage = (float)adc0 * 0.0000078125f;
  // Convert voltage to PPFD using user sensor sensitivity (V per umol)
  float ppfd = voltage / SENSOR_SENSITIVITY_V_PER_UMOL;

  // Update rolling buffer (circular)
  ppfdBuf[ppfdBufIndex] = ppfd;
  ppfdBufIndex = (ppfdBufIndex + 1) % PPFD_BUF_SIZE;
  if (ppfdBufCount < PPFD_BUF_SIZE) ppfdBufCount++;

  // Update daily maximum PPFD (for Field 8)
  if (ppfd > dailyMaxPPFD) dailyMaxPPFD = ppfd;

  // Compute averages and max over buffer
  float ppfdAvg20s = ppfdBufferAvg();
  float ppfdMax20s = ppfdBufferMax(); // not sent, but available if needed

  // ===================== THRESHOLD LOGIC (HYSTERESIS + STEADY TIME) =====================
  float lowTh  = PPFD_THRESHOLD - PPFD_HYSTERESIS;
  float highTh = PPFD_THRESHOLD + PPFD_HYSTERESIS;

  bool wantOn = false, wantOff = false;

  if (isNight) {
    // If it's night, prefer off
    wantOff = true;
  } else {
    // If PPFD is persistently below low threshold -> want ON
    if (ppfd < lowTh) {
      if (!belowTimerStart) belowTimerStart = now;
      if (now - belowTimerStart >= STEADY_TIME_MS) wantOn = true;
    } else {
      belowTimerStart = 0;
    }

    // If PPFD is persistently above high threshold -> want OFF
    if (ppfd > highTh) {
      if (!aboveTimerStart) aboveTimerStart = now;
      if (now - aboveTimerStart >= STEADY_TIME_MS) wantOff = true;
    } else {
      aboveTimerStart = 0;
    }
  }

  // ===================== ACTION: LOCAL CONTROL & MQTT PUBLISH =====================
  // Apply toggle if appropriate and not within toggle lockout
  if ((wantOn && !lightState) || (wantOff && lightState)) {
    if (now - lastToggleMillis >= TOGGLE_LOCK_MS) {
      lightState = wantOn; // if wantOn is true we turn on, else turn off
      // Publish the change to your local MQTT topic (so other systems know)
      client.publish(mqtt_topic, lightState ? "1" : "0");
      lastToggleMillis = now;
      lastChangeMillis = now;
      // reset the steady timers after a toggle
      belowTimerStart = aboveTimerStart = 0;
      Serial.println("Local auto-toggle applied");
    }
  }

  // ===================== ENERGY / TIME ACCUMULATION =====================
  if (lastEnergyUpdate == 0) lastEnergyUpdate = now;
  unsigned long deltaMs = now - lastEnergyUpdate;
  if (deltaMs > 0) {
    // compute elapsed hours and seconds to increment counters proportionally
    float hours = (float)deltaMs / 3600000.0f;
    unsigned long secs = deltaMs / 1000UL;
    if (lightState) {
      float e = LAMP_POWER_KW * hours;
      energyToday += e;
      energyWeek  += e;
      energyMonth += e;
      energyTotal += e;
      lightOnTodaySeconds += secs;
    }
    lastEnergyUpdate = now;
  }

  // ===================== THINGSPEAK UPLOAD (every THINGSPEAK_INTERVAL_MS) =====================
  unsigned long timeInCurrent_s = (now - lastChangeMillis) / 1000UL;
  if (now - lastThingSpeakMillis >= THINGSPEAK_INTERVAL_MS) {
    // publish telemetry: this will internally enforce the 20s lock as well
    publishToThingSpeak(ppfdAvg20s, dailyMaxPPFD, timeInCurrent_s);
    // lastThingSpeakMillis is set inside publishToThingSpeak()
  }

  // ===================== DEBUG / SERIAL OUTPUT =====================
  unsigned long belowElapsed = (belowTimerStart ? (now - belowTimerStart) : 0UL);
  unsigned long aboveElapsed = (aboveTimerStart ? (now - aboveTimerStart) : 0UL);
  unsigned long timeInCurrent = (now - lastChangeMillis) / 1000UL;

  Serial.printf("%02d:%02d | %s | Light=%s | PPFD=%.1f | avg20=%.1f | maxDay=%.1f\n",
                hour, minute, (isNight ? "NIGHT" : "DAY"), (lightState ? "ON" : "OFF"),
                ppfd, ppfdAvg20s, dailyMaxPPFD);
  Serial.printf(" TimeInCur=%lus | Today=%.4fkWh ₪%.2f | Month=%.3fkWh ₪%.2f\n",
                timeInCurrent, energyToday, energyToday * PRICE_PER_KWH,
                energyMonth, energyMonth * PRICE_PER_KWH);
  Serial.printf(" below_t=%lums above_t=%lums lastTS=%lums\n\n",
                belowElapsed, aboveElapsed, (now - lastThingSpeakMillis));

  // Sample roughly once per second (keeps buffer ~20s)
  delay(1000);
}
