# Agrotech_Lab_2026 project: 
#### Made by Tal Stoler & Hadas Ekshtein & Yuval Masad & Amit Schraub
### Motivation
The goal of this project is to maintain optimal growing conditions for plants even during winter days when natural light is limited. Using a light sensor, the system detects when illumination drops below 200 µmol photons/m² and automatically activates a supplemental light source. In addition, the system monitors energy consumption and turns off the light when it is no longer needed, optimizing both plant growth and cost efficiency. This approach ensures sufficient light for photosynthesis and healthy development while saving energy and adapting intelligently to environmental conditions.

$ . 

---

### Project Overview
The project aims to maximize plant growth through the optimization of the photosynthesis process. The system monitors light intensity in real-time and automatically activates artificial lighting to compensate for deficiencies whenever levels drop below a critical threshold. (אפשר להוסיף קישור לקוד),(אפשר להוסיף נפח לפסקה בעזרת הסבר על החיישנים וכו' כשתהיה מערכת שרצה)

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

  ### Schematics

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
