#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ================= ADS =================
Adafruit_ADS1115 ads;

// ================= WIFI =================
const char* ssid = "agrotech";
const char* password = "1Afuna2gezer";

// ================= MQTT =================
const char* mqtt_server   = "192.168.0.102";
const int   mqtt_port     = 1883;
const char* mqtt_user     = "mqtt-user";
const char* mqtt_password = "1234";
const char* mqtt_topic    = "/greenhouse/sockets/socket1";

// ================= TIME (Israel) =================
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;
const int   daylightOffset_sec = 3600;

WiFiClient espClient;
PubSubClient client(espClient);

// ================= THINGSPEAK =================
const char* TS_HOST = "api.thingspeak.com";
const char* TS_WRITE_KEY = "JSFT7BA0D9DANFE4";
const char* TS_READ_KEY  = "XEKG8SWJJLVL5O87";
const long  TS_CHANNEL_ID = 3223345;
const unsigned long THINGSPEAK_INTERVAL_MS = 15000UL; // free tier minimum
unsigned long lastThingSpeakMillis = 0;

// ================= LIGHT =================
bool lightState = false;
unsigned long lastChangeMillis = 0; // when lightState last changed

// ================= SENSOR =================
const float SENSOR_SENSITIVITY_V_PER_UMOL = 0.00000768;

// ================= PPFD CONTROL =================
const float PPFD_THRESHOLD  = 200.0;
const float PPFD_HYSTERESIS = 30.0;

const unsigned long STEADY_TIME_MS = 10000UL;
const unsigned long TOGGLE_LOCK_MS = 30000UL;

unsigned long lastToggleMillis = 0;
unsigned long belowTimerStart = 0;
unsigned long aboveTimerStart = 0;

// ================= POWER & COST =================
const float LAMP_POWER_KW = 0.065;      // 65W
const float PRICE_PER_KWH = 0.64;       // ₪ / kWh

float energyToday = 0.0f;
float energyWeek  = 0.0f;
float energyMonth = 0.0f;
float energyTotal = 0.0f;

unsigned long lastEnergyUpdate = 0;
int lastDay = -1, lastWeek = -1, lastMonth = -1;

// ================= PPFD ROLLING BUFFER (15 samples ~ 15s) =================
const int PPFD_BUF_SIZE = 15;
float ppfdBuf[PPFD_BUF_SIZE];
int ppfdBufIndex = 0;
int ppfdBufCount = 0;

// =================================================

void setup_wifi() {
  Serial.print("WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm t;
  while (!getLocalTime(&t)) delay(500);
  Serial.println("Time synced");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  reconnect();

  Wire.begin(21, 22);
  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 NOT FOUND");
    while (1) delay(1000);
  }
  ads.setGain(GAIN_SIXTEEN);

  lastEnergyUpdate = millis();
  lastThingSpeakMillis = millis();
  lastChangeMillis = millis();

  // init buffer
  ppfdBufCount = 0;
  ppfdBufIndex = 0;

  Serial.println("SYSTEM READY");
}

// compute average & max from buffer
float ppfdBufferAvg() {
  if (ppfdBufCount == 0) return 0.0f;
  float s = 0.0f;
  for (int i=0;i<ppfdBufCount;i++) s += ppfdBuf[i];
  return s / (float)ppfdBufCount;
}
float ppfdBufferMax() {
  if (ppfdBufCount == 0) return 0.0f;
  float m = ppfdBuf[0];
  for (int i=1;i<ppfdBufCount;i++) if (ppfdBuf[i] > m) m = ppfdBuf[i];
  return m;
}

// Read remote command from ThingSpeak field7 last value (returns -1 if none, 0 or 1)
int readThingSpeakField7Last() {
  WiFiClient http;
  if (!http.connect(TS_HOST, 80)) {
    Serial.println("TS: connect failed");
    return -1;
  }

  // endpoint: /channels/{id}/fields/7/last?api_key=READKEY
  String req = String("GET /channels/") + String(TS_CHANNEL_ID) + "/fields/7/last?api_key=" + TS_READ_KEY + " HTTP/1.1\r\n" +
               "Host: " + TS_HOST + "\r\n" +
               "Connection: close\r\n\r\n";
  http.print(req);

  // wait for response and read headers
  unsigned long start = millis();
  while (http.available() == 0) {
    if (millis() - start > 2000) { http.stop(); return -1; }
    delay(5);
  }

  // skip headers
  bool isBody = false;
  String body = "";
  while (http.available()) {
    String line = http.readStringUntil('\n');
    if (!isBody) {
      if (line == "\r") { isBody = true; }
    } else {
      body += line;
    }
  }
  http.stop();

  body.trim();
  if (body.length() == 0) return -1;

  // body should be the last value as plain text e.g. "1" or "0"
  if (body == "1") return 1;
  if (body == "0") return 0;
  // sometimes it may contain JSON or newline; try to parse number
  int v = body.toInt();
  if (v == 0 && body != "0") return -1;
  if (v == 1) return 1;
  return -1;
}

// Publish telemetry to ThingSpeak
void publishToThingSpeak(int remoteCmdSeen, float ppfdAvg, float ppfdMax, float energyToday_kwh, float energyWeek_kwh, float energyMonth_kwh, unsigned long timeInCurrent_s) {
  WiFiClient http;
  if (!http.connect(TS_HOST, 80)) {
    Serial.println("TS: connect failed (publish)");
    return;
  }

  // build payload
  String body = "api_key=";
  body += TS_WRITE_KEY;
  body += "&field1="; body += (lightState ? "1" : "0");                // Grow Light Stat
  body += "&field2="; body += String(timeInCurrent_s);                // Time in Current (s)
  body += "&field3="; body += String(energyToday_kwh, 4);             // Daily Electricity
  body += "&field4="; body += String(energyWeek_kwh, 4);              // Weekly Electric
  body += "&field5="; body += String(energyMonth_kwh, 4);             // Monthly Electri
  body += "&field6="; body += String(ppfdAvg, 2);                     // PPFD avg last 15s
  if (remoteCmdSeen >= 0) {
    body += "&field7="; body += String(remoteCmdSeen);               // Remote command seen (0/1)
  } else {
    body += "&field7="; body += "";                                   // empty
  }
  body += "&field8="; body += String(ppfdMax, 1);                     // PPFD max last 15s

  String req = String("POST /update HTTP/1.1\r\n") +
               "Host: " + TS_HOST + "\r\n" +
               "Connection: close\r\n" +
               "Content-Type: application/x-www-form-urlencoded\r\n" +
               "Content-Length: " + String(body.length()) + "\r\n\r\n" +
               body;

  http.print(req);

  // read response (optional)
  unsigned long start = millis();
  while (http.available() == 0) {
    if (millis() - start > 3000) { http.stop(); return; }
    delay(5);
  }
  // skip response for brevity
  while (http.available()) {
    String line = http.readStringUntil('\n');
    // you can parse the response if you want (TS returns entry id or 0)
  }
  http.stop();
  Serial.println("ThingSpeak: uploaded");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  // ---------- TIME ----------
  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int nowMin = hour * 60 + minute;

  // Reset counters by date/week/month
  if (timeinfo.tm_mday != lastDay) {
    energyToday = 0.0f;
    lastDay = timeinfo.tm_mday;
  }
  if ((timeinfo.tm_yday / 7) != lastWeek) {
    energyWeek = 0.0f;
    lastWeek = timeinfo.tm_yday / 7;
  }
  if (timeinfo.tm_mon != lastMonth) {
    energyMonth = 0.0f;
    lastMonth = timeinfo.tm_mon;
  }

  // ---------- NIGHT ----------
  bool isNight = (nowMin >= 21*60) || (nowMin < 5*60);

  // ---------- SENSOR ----------
  int16_t adc0 = ads.readADC_SingleEnded(0);
  float voltage = adc0 * 0.0000078125f;
  float ppfd = voltage / SENSOR_SENSITIVITY_V_PER_UMOL;

  // store in rolling buffer (1 sample per loop, ~1s)
  ppfdBuf[ppfdBufIndex] = ppfd;
  ppfdBufIndex = (ppfdBufIndex + 1) % PPFD_BUF_SIZE;
  if (ppfdBufCount < PPFD_BUF_SIZE) ppfdBufCount++;

  float ppfdAvg = ppfdBufferAvg();
  float ppfdMax = ppfdBufferMax();

  // ---------- THRESHOLDS & STEADY ----------
  float lowTh  = PPFD_THRESHOLD - PPFD_HYSTERESIS;
  float highTh = PPFD_THRESHOLD + PPFD_HYSTERESIS;

  bool wantOn = false, wantOff = false;

  if (timeSynced && isNight) {
    wantOff = true;
  } else {
    if (ppfd < lowTh) {
      if (!belowTimerStart) belowTimerStart = now;
      if (now - belowTimerStart >= STEADY_TIME_MS) wantOn = true;
    } else belowTimerStart = 0;

    if (ppfd > highTh) {
      if (!aboveTimerStart) aboveTimerStart = now;
      if (now - aboveTimerStart >= STEADY_TIME_MS) wantOff = true;
    } else aboveTimerStart = 0;
  }

  // ---------- LOCAL vs REMOTE CONTROL ----------
  // Remote read & ThingSpeak upload happen every THINGSPEAK_INTERVAL_MS
  if (now - lastThingSpeakMillis >= THINGSPEAK_INTERVAL_MS) {
    // read remote command from field7
    int remoteCmd = readThingSpeakField7Last(); // -1 / 0 / 1

    // if remote command exists and is different than current state, apply (respect lockout)
    if (remoteCmd >= 0) {
      if ((remoteCmd == 1 && !lightState) || (remoteCmd == 0 && lightState)) {
        if (now - lastToggleMillis >= TOGGLE_LOCK_MS) {
          lightState = (remoteCmd == 1);
          client.publish(mqtt_topic, lightState ? "1" : "0");
          lastToggleMillis = now;
          lastChangeMillis = now;
          belowTimerStart = aboveTimerStart = 0;
          Serial.println("Remote command applied from ThingSpeak");
        } else {
          Serial.println("Remote command suppressed by lockout");
        }
      }
    }

    // apply local PPFD-based toggle only if remote didn't already change state
    if ((wantOn && !lightState) || (wantOff && lightState)) {
      if (now - lastToggleMillis >= TOGGLE_LOCK_MS) {
        lightState = wantOn;
        client.publish(mqtt_topic, lightState ? "1" : "0");
        lastToggleMillis = now;
        lastChangeMillis = now;
        belowTimerStart = aboveTimerStart = 0;
        Serial.println("Local auto-toggle applied");
      }
    }

    // Update energy using elapsed time since last update
    if (lastEnergyUpdate == 0) lastEnergyUpdate = now;
    float hours = (now - lastEnergyUpdate) / 3600000.0f;
    if (lightState) {
      float e = LAMP_POWER_KW * hours;
      energyToday += e;
      energyWeek  += e;
      energyMonth += e;
      energyTotal += e;
    }
    lastEnergyUpdate = now;

    // time in current state (seconds)
    unsigned long timeInCurrent_s = (now - lastChangeMillis) / 1000UL;

    // publish to ThingSpeak
    publishToThingSpeak(remoteCmd, ppfdAvg, ppfdMax, energyToday, energyWeek, energyMonth, timeInCurrent_s);

    lastThingSpeakMillis = now;
  } else {
    // Apply local PPFD-based toggle outside of ThingSpeak timing (but only if remote didn't recently act)
    if ((wantOn && !lightState) || (wantOff && lightState)) {
      if (now - lastToggleMillis >= TOGGLE_LOCK_MS) {
        lightState = wantOn;
        client.publish(mqtt_topic, lightState ? "1" : "0");
        lastToggleMillis = now;
        lastChangeMillis = now;
        belowTimerStart = aboveTimerStart = 0;
        Serial.println("Local auto-toggle applied (non-TS cycle)");
      }
    }

    // Update energy incrementally every loop (if on)
    float hoursSmall = (now - lastEnergyUpdate) / 3600000.0f;
    if (hoursSmall > 0.0f) {
      if (lightState) {
        float e = LAMP_POWER_KW * hoursSmall;
        energyToday += e;
        energyWeek  += e;
        energyMonth += e;
        energyTotal += e;
      }
      lastEnergyUpdate = now;
    }
  }

  // ---------- DEBUG ----------
  unsigned long belowElapsed = (belowTimerStart ? (now - belowTimerStart) : 0UL);
  unsigned long aboveElapsed = (aboveTimerStart ? (now - aboveTimerStart) : 0UL);
  unsigned long timeInCurrent = (now - lastChangeMillis) / 1000UL;

  Serial.printf("%02d:%02d | %s | Light=%s | PPFD=%.1f | avg15=%.1f | max15=%.1f\n",
                hour, minute, (isNight ? "NIGHT" : "DAY"), (lightState ? "ON" : "OFF"),
                ppfd, ppfdAvg, ppfdMax);
  Serial.printf(" TimeInCur=%lus | Today=%.4fkWh ₪%.2f | Month=%.3fkWh ₪%.2f\n",
                timeInCurrent, energyToday, energyToday * PRICE_PER_KWH, energyMonth, energyMonth * PRICE_PER_KWH);
  Serial.printf(" below_t=%lums above_t=%lums lastTS=%lums\n\n", belowElapsed, aboveElapsed, (now - lastThingSpeakMillis));

  delay(1000);
}
