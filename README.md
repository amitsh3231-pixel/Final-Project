# Agrotech_Lab_2026 project: 
#### Made by Tal Stoler & Hadas Ekshtein & Yuval Masad & Amit Schraub
### Project goal
This project aims to ensure optimal plant growth even during the darker winter days, while intelligently minimizing electricity use to lower energy costs and promote sustainable cultivation.

--

### Backround
Photosynthesis is a series of light‑driven redox reactions in the chloroplasts that convert solar energy into chemical energy stored in carbohydrates. In the light reactions, photon absorption by photosystems II and I drives electron transport from water to NADP⁺, generating NADPH and establishing a proton gradient used by ATP synthase to produce ATP. In the Calvin cycle, ATP and NADPH power CO₂ fixation by Rubisco and subsequent reduction and regeneration reactions, producing triose phosphates that support biomass accumulation and yield.
<img width="230" height="305" alt="image" src="https://github.com/user-attachments/assets/70065484-37e7-41c5-be5f-8c94213aa6ce" />


---

### Hardware
- ESP32
- Quantum sensor (humidity)
- ** Pyrnometer(light)
- ADC (ADS1115 model)
- Growlight (Driver)
- OLED display
- LEDs
- Wires
  
---
### System wiring diagram
by tal
  ### Steps for building the project

1.Place the ESP32 on a breadboard and connect it to the ADS1115 module and PAR sensor according to the wiring schematic of the system.

2.Hang the lamp above the plant and adjust its height until the PAR meter reads approximately 200 µmol photons/m² (or any other target value you choose) at the plant canopy.

3.Connect the lamp to the smart switch configured for MQTT control, then upload the project code to the ESP32 so it can switch the lamp on and off based on the PAR threshold.

4.Position the PAR sensor close to the plant so it represents the light at canopy level, but slightly offset from the lamp beam to minimize direct influence from the artificial light.

5.Set up a ThingSpeak channel and link it to the ESP32 data stream in order to log PAR values, lamp on/off status, and estimated power consumption over time, enabling analysis of how long the lamp was active and how much energy it used.

  # notes!!
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

// רגישות החיישן RY-GH:
// 7.68 µV לכל 1 µmol·m⁻²·s⁻¹  =>  7.68e-6 וולט
const float SENSOR_SENSITIVITY_V_PER_UMOL = 0.00000768;  // V per µmol/m2/s

void setup() {
  Serial.begin(115200);

  // I2C ל‑ESP32: SDA=21, SCL=22
  Wire.begin(21, 22);

  // התחלת ה‑ADS1115 בכתובת ברירת מחדל 0x48
  if (!ads.begin(0x48)) {           // Adafruit ADS1115 [web:24]
    Serial.println("ADS1115 לא נמצא, בדקי חיבורים");
    while (1);
  }

  // טווח מדידה: ±4.096V (GAIN_ONE)
  // זה נותן רזולוציה של ~0.125 mV לכל count
  ads.setGain(GAIN_SIXTEEN);            // [web:24]

  Serial.println("PAR Sensor + ADS1115 + ESP32 מוכן לקריאה");
}

void loop() {
  // קריאה חד‑קוטבית מהערוץ A0 (החיישן מחובר ל‑A0 מול GND)
  int16_t adc0 = ads.readADC_SingleEnded(0);

  // המרת ערך גולמי למתח (וולט)
  // 4.096V / 32768 ≈ 0.000125 V לכל count ב‑GAIN_SIXTEEN
  float voltage = adc0 * 0.0000078125;

  // המרת מתח ל‑PPFD (µmol·m⁻²·s⁻¹)
  float ppfd = voltage / SENSOR_SENSITIVITY_V_PER_UMOL;

  Serial.print("ADC: ");
  Serial.print(adc0);
  Serial.print("  V: ");
  Serial.print(voltage, 6);
  Serial.print("  PPFD: ");
  Serial.print(ppfd, 1);
  Serial.println(" umol/m2/s");

  delay(1000);  // קריאה פעם בשנייה
}
  חישוב צריכת חשמל של הנורה ועלות (הצגת דוגמת חישוב)
  מרחק האור מהצמח
