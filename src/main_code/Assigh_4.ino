#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "Adafruit_SHT31.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_SHT31 sht31 = Adafruit_SHT31();

const int PIN_BUTTON = 13;
const int PIN_LED_BLUE = 25;
const int PIN_LED_YELLOW = 26;
const int PIN_LED_RED = 14;

const int PIN_THERMISTOR = 34;
const int PIN_PHOTO = 35;

int menuState = 0;
int buttonState;             
int lastButtonState = HIGH;  
unsigned long lastDebounceTime = 0;  
unsigned long debounceDelay = 50;    

unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  u8g2.begin();
  
  if (!sht31.begin(0x44)) {
    Serial.println("Error: SHT31 not found.");
  }
}

void loop() {
  int reading = digitalRead(PIN_BUTTON);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        menuState++;
        if (menuState > 3) menuState = 0;
      }
    }
  }
  lastButtonState = reading;

  float shtTemp = sht31.readTemperature();
  float shtHum = sht31.readHumidity();

  int thermistorRaw = analogRead(PIN_THERMISTOR);
  float thermistorTemp = map(thermistorRaw, 0, 4095, 50, 0); 

  int lightRaw = analogRead(PIN_PHOTO);
  int lightPercent = map(lightRaw, 0, 4095, 100, 0);
  lightPercent = constrain(lightPercent, 0, 100);

  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  bool timeToPrint = (millis() - lastPrintTime > 500);
  if (timeToPrint) lastPrintTime = millis();

  switch (menuState) {
    case 0:
      digitalWrite(PIN_LED_BLUE, HIGH);
      digitalWrite(PIN_LED_YELLOW, HIGH);
      digitalWrite(PIN_LED_RED, HIGH);
      break;

    case 1:
      if (shtTemp > 28 || shtHum > 70) digitalWrite(PIN_LED_RED, HIGH);
      else if (shtTemp < 18 || shtHum < 30) digitalWrite(PIN_LED_BLUE, HIGH);
      else digitalWrite(PIN_LED_YELLOW, HIGH);
      
      if (timeToPrint) {
        Serial.print("[SHT31] T:"); Serial.print(shtTemp); Serial.print(" H:"); Serial.println(shtHum);
      }
      break;

    case 2:
      if (thermistorTemp > 28) digitalWrite(PIN_LED_RED, HIGH);
      else if (thermistorTemp < 18) digitalWrite(PIN_LED_BLUE, HIGH);
      else digitalWrite(PIN_LED_YELLOW, HIGH);

      if (timeToPrint) {
         Serial.print("[Therm] Raw:"); Serial.print(thermistorRaw); Serial.print(" Calc:"); Serial.println(thermistorTemp);
      }
      break;

    case 3:
      if (lightPercent > 70) digitalWrite(PIN_LED_BLUE, HIGH);
      else if (lightPercent < 30) digitalWrite(PIN_LED_RED, HIGH);
      else digitalWrite(PIN_LED_YELLOW, HIGH);

      if (timeToPrint) {
         Serial.print("[Light] Raw:"); Serial.print(lightRaw); Serial.print(" %:"); Serial.println(lightPercent);
      }
      break;
  }

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);

    switch (menuState) {
      case 0:
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        u8g2.drawStr(10, 35, "Weather Station");
        
        u8g2.setFont(u8g2_font_profont10_mr);
        u8g2.drawStr(30, 55, "Press Button ->");
        break;

      case 1:
        u8g2.drawStr(35, 10, "- SHT31 -");
        u8g2.setCursor(0, 35);
        u8g2.print("Temp: "); u8g2.print(shtTemp, 1); u8g2.print(" C");
        u8g2.setCursor(0, 55);
        u8g2.print("Hum:  "); u8g2.print(shtHum, 1); u8g2.print(" %");
        break;

      case 2:
        u8g2.drawStr(25, 10, "- Thermistor -");
        u8g2.setCursor(0, 35);
        u8g2.print("Temp: "); u8g2.print(thermistorTemp, 1); u8g2.print(" C");
        u8g2.setCursor(0, 55);
        u8g2.print("(Raw: "); u8g2.print(thermistorRaw); u8g2.print(")");
        break;

      case 3:
        u8g2.drawStr(40, 10, "- Light -");
        
        u8g2.setFont(u8g2_font_profont10_mr);
        u8g2.drawStr(30, 20, "(Photoresistor)");
        
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawFrame(14, 30, 90, 10);
        u8g2.drawBox(14, 30, map(lightPercent,0,100,0,90), 10); 
        u8g2.setCursor(35, 55);
        u8g2.print("Level: "); u8g2.print(lightPercent); u8g2.print(" %");
        break;
    }
  } while (u8g2.nextPage());
}
