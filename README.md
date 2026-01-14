# Agrotech_Lab_2026 project: 
#### Made by Tal Stoler & Hadas Ekshtein & Yuval Masad & Amit Schraub
### Project goal
| Project goal |  |
| --- | --- |
| This project aims to ensure optimal plant growth even during the darker winter days, while intelligently minimizing electricity use to lower energy costs and promote sustainable cultivation. | <img src="https://github.com/user-attachments/assets/5237fe42-8f64-4438-83e8-55b7c98c6e31" width="250"> |




--

### Backround
Photosynthesis is a series of light‑driven redox reactions in the chloroplasts that convert solar energy into chemical energy stored in carbohydrates. In the light reactions, photon absorption by photosystems II and I drives electron transport from water to NADP⁺, generating NADPH and establishing a proton gradient used by ATP synthase to produce ATP. In the Calvin cycle, ATP and NADPH power CO₂ fixation by Rubisco and subsequent reduction and regeneration reactions, producing triose phosphates that support biomass accumulation and yield.
<img width="230" height="305" alt="image" src="https://github.com/user-attachments/assets/70065484-37e7-41c5-be5f-8c94213aa6ce" />


---

### Hardware
- ESP32
- Quantum sensor 
- ADC (ADS1115 model)
- Growlight
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

  צמח
