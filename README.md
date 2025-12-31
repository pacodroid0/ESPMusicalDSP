# ESPDSP - ESP32 Hi-Fi Smart Receiver & DSP Amplifier

**ESPDSP** is a firmware project that transforms an ESP32 microcontroller into a high-fidelity digital audio processor and integrated amplifier controller. It combines Bluetooth A2DP streaming, analog inputs, and a powerful software-defined DSP engine offering features typically found in high-end vintage receivers.

The system acts as a pre-amplifier/processor for Class-T (e.g., TA2024) or Class-D power stages.

---

## 🚀 Key Features (Firmware v1.0)

### 🎧 Inputs & Sources

* **Bluetooth A2DP Sink:** High-quality wireless streaming from smartphones/PC.
* **Analog AUX Input:** 24-bit ADC support (PCM1808) for Turntables, Tape Decks, or Line sources.
* **Signal Generator:** Built-in Sine, Pink Noise, White Noise, and Frequency Sweep for system testing.

### 🎛️ Digital Signal Processing (DSP) Engine

* **10-Band Graphic Equalizer:** Fully adjustable via Web Interface.
* **Adaptive Loudness:** Fletcher-Munson curve implementation that automatically boosts bass/treble at low volumes to match human hearing.
* **Stereo Expander:** Mid-Side processing to widen the soundstage.
* **Vintage Emulation:**
* **RIAA Preamp:** Software phono stage for connecting vinyl turntables directly to Line inputs.
* **Dolby B NR:** Tape hiss reduction simulation.



### 💻 Control Interface

* **Smart Buttons:** Multi-function physical buttons for tactile control.
* **Web Interface (SoftAP):** Mobile-friendly dashboard hosted on the ESP32 (default IP: `192.168.4.1`) for EQ configuration and system settings.
* **Non-Volatile Memory:** Saves Volume, Input Mode, EQ curves, and Effect states across reboots.

---

## 🛠️ Hardware Architecture

### Core Components

* **MCU:** ESP32 WROVER (Recommended for PSRAM, though standard ESP32 works).
* **DAC (Output):** PCM5102 (I2S, 32-bit).
* **ADC (Input):** PCM1808 (I2S, 24-bit).
* **Amplifier:** Tripath TA2024 (or TPA3116) Class-T module.

### Pinout Configuration

Defined in `pindef.h`:

| Function | Pin (ESP32) | Note |
| --- | --- | --- |
| **DAC BCK** | 26 | I2S Output |
| **DAC WS** | 25 | I2S Output |
| **DAC DATA** | 22 | I2S Output |
| **ADC BCK** | 14 | I2S Input |
| **ADC WS** | 18 | I2S Input |
| **ADC DATA** | 13 | I2S Input |
| **Vol Up** | 32 | Button (Pull-up) |
| **Vol Down** | 33 | Button (Pull-up) |
| **Source** | 4 | Button (Pull-up) |
| **Preset/FX** | 5 | Button (Pull-up) |
| **Pair** | 27 | Button (Pull-up) |

**

---

## 📖 User Guide

### Physical Controls

* **VOL+ / VOL-**: Adjust Master Volume (0-30).
* *Combo Press (Both)*: Toggle Wi-Fi Access Point for Web Config.


* **SOURCE**:
* *Short Press*: Toggle Bluetooth / AUX.
* *Long Press (>3s)*: Activate/Deactivate Signal Generator Mode.


* **PRESET (Effect Button)**:
* *1 Click*: Toggle **Loudness** (LED feedback).
* *2 Clicks*: Toggle **Stereo Expander**.
* *Long Press (in AUX Mode)*: Cycle Preamp Modes (Line -> RIAA Phono -> Dolby NR).



### Web Configuration

1. Enable Wi-Fi by pressing VOL+ and VOL- together.
2. Connect to Wi-Fi Network: `ESPDSP` (Password: `ESPDSP`).
3. Navigate to `http://192.168.4.1`.
4. Use the interface to adjust the 10-Band EQ, Input Gain, and save Presets.

---

## 📅 Roadmap (Upcoming FW v2.0)

The project is currently evolving into a full "Receiver". Planned features for the next release:

* [ ] **FM Radio Integration:** Support for RDA5807M module with RDS decoding.
* [ ] **Display Support:** Transition from LEDs to I2C LCD (20x4) or OLED for status/VU meters.
* [ ] **Expanded Inputs:** Hardware support for 74HC4052 Multiplexer (Radio, Phono, Tape, Line).
* [ ] **Advanced Vintage DSP:** Implementation of DBX Type II Expansion (Decoded).
* [ ] **Bluetooth Source:** Ability to transmit AUX/Phono audio to Bluetooth Headphones.

---

## 🔧 Installation & Compilation

1. **Environment:** PlatformIO (recommended) or Arduino IDE.
2. **Dependencies:**
* `ESP32-A2DP` (by Phil Schatzmann)
* `ArduinoJson`
* `WiFi`, `WebServer`, `Preferences` (Standard ESP32 libs)


3. **Setup:**
* Clone the repo.
* Verify `pindef.h` matches your wiring.
* Upload to ESP32.
* *Note:* Ensure GPIO 15 is NOT pulled high during boot (it is used for LEDs in v2.0 but acts as a strapping pin).

---

## 📄 License

This project is open-source. Feel free to fork, modify, and contribute.

*Disclaimer: This is a DIY audio project. Ensure proper power supply isolation to prevent ground loops and noise, especially when dealing with Analog ADC inputs.*
