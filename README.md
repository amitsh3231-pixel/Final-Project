# Agrotech_Lab_2026 project- light compensation system
#### Made by Tal Stoler & Hadas Ekshtein & Yuval Masad & Amit Schraub
### Project goal
This project aims to ensure optimal plant growth even during the darker winter days, while intelligently minimizing electricity use to lower energy costs and promote sustainable cultivation. 




---

### Backround
Photosynthesis is a series of light‑driven redox reactions in the chloroplasts that convert solar energy into chemical energy stored in carbohydrates. In the light reactions, photon absorption by photosystems II and I drives electron transport from water to NADP⁺, generating NADPH and establishing a proton gradient used by ATP synthase to produce ATP. In the Calvin cycle, ATP and NADPH power CO₂ fixation by Rubisco and subsequent reduction and regeneration reactions, producing triose phosphates that support biomass accumulation and yield.
In our project, we used a cucumber plant for which the literature reports an optimal light level of around 200 µmol photons/m² at the canopy.

​
<img width="230" height="305" alt="image" src="https://github.com/user-attachments/assets/70065484-37e7-41c5-be5f-8c94213aa6ce" />


---

### Hardware
- ESP32
- PAR Quantum sensor 
- ADC (ADS1115 model)
- Growlight
- Wires
- Smart socket
  
---
### System wiring diagram
<img width="300" height="400" alt="image" src="https://github.com/user-attachments/assets/b4d3c369-d850-4a6a-8668-dfd89c09cfcb" />



  ### Steps for building the project

1. Place the ESP32 on a breadboard and connect it to the ADS1115 module and PAR sensor according to the wiring schematic of the system.

2. Hang the lamp above the plant and adjust its height until the PAR meter reads approximately 200 µmol photons/m² (or any other target value you choose) at the plant canopy.

3. Connect the lamp to the smart switch configured for MQTT control, then upload the project code to the ESP32 so it can switch the lamp on and off based on the PAR threshold.

4. Position the PAR sensor close to the plant so it represents the light at canopy level, but slightly offset from the lamp beam to minimize direct influence from the artificial light.

5. Set up a ThingSpeak channel and link it to the ESP32 data stream in order to log PAR values, lamp on/off status, and estimated power consumption over time, enabling analysis of how long the lamp was active and how much energy it used.

---
####
### Control safeguards
The control code includes several safeguards: automatic nighttime shutoff (9pm-5am) to give plants essential darkness for their circadian rhythm and to avoid light stress, a hysteresis band around the 200 µmol m⁻² s⁻¹ target to prevent rapid flickering when PPFD hovers near the threshold, and delays between switching events to avoid excessive toggling of the grow light and smart socket.
### Irrigation Schedule
Plants were irrigated independently using a fixed schedule of three watering events per day, each lasting 3 minutes, to ensure non‑limiting water availability
### PPFD Measurement
PPFD (Photosynthetic Photon Flux Density) counts photons only in the photosynthetically active radiation range of 400–700 nm.
####
---
<div style="display: flex; gap: 20px;">
  <img src="https://github.com/user-attachments/assets/ff78740d-13ec-4610-94b8-17bb515fce0d" width="220" height="300" alt="setup">
  ![תמונה טל וארז](https://github.com/user-attachments/assets/a0df8f3b-eee8-4d30-8819-ce623283db39)
  ![תמונה הדס וטל](https://github.com/user-attachments/assets/064f9bae-18d5-4c31-9246-4e28b833230f)
</div>

<div style="display: flex; justify-content: center; margin-top: 20px;">
  <img src="https://github.com/user-attachments/assets/981297ce-0e2a-4b6d-923d-06a2085a2acb" width="300" height="400" alt="lamp">
</div>







