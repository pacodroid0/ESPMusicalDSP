#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <Wire.h>

// --- LOCAL INCLUDES ---
#include "pindef.h"
#include "secrets.h"
#include "dsp_engine.h"
#include "bluestream.h"
#include "fmradio.h"
#include "displayinfo.h"
#include "phbuttons.h"
#include "pnoise.h" 

// --- GLOBAL OBJECTS ---
Preferences preferences;
WebServer server(80);
AudioDSP dsp;
BlueStream bt;
RadioManager radio;
LiquidCrystal_I2C lcd(0x27, 20, 4);
DisplayUI ui(&lcd);
ButtonManager buttons;

// --- STATE VARIABLES ---
enum OperationMode { MODE_BT, MODE_RADIO, MODE_AUX, MODE_GEN };
OperationMode currentMode = MODE_BT;

int volume = 15;
bool wifiActive = false;
bool isTxMode = false; // Loaded from preferences

// Generator Globals
int genSignalType = 0; 
bool genActive = false;
float genFreqStart = 440.0;
float genFreqEnd = 440.0;
float genPeriod = 10.0;
unsigned long sweepStartTime = 0;
double currentPhase = 0;

// Display & Meters
unsigned long lastDisplayUpdate = 0;
String btTitle = "";
String btArtist = "";
int vuLeft = 0;
int vuRight = 0;

// Radio UI
bool radioShowMemories = false;
int radioCursor = 1;

// --- WEB SERVER ---
#include "web_server.h"

// --- FORWARD DECLARATIONS ---
void bt_volume_callback(int vol);
void bt_data_callback(const uint8_t *data, uint32_t len);
void bt_metadata_callback(uint8_t id, const uint8_t *text);
int32_t bt_source_data_callback(Frame *data, int32_t frame_count);

// --- I2S CONFIGURATION ---
void setupI2S_DAC() {
    i2s_config_t dac_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 128,
        .use_apll = true
    };
    i2s_driver_install(I2S_NUM_0, &dac_config, 0, NULL);
    i2s_pin_config_t dac_pins = {
        .bck_io_num = PIN_DAC_BCK,
        .ws_io_num = PIN_DAC_WS,
        .data_out_num = PIN_DAC_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(I2S_NUM_0, &dac_pins);
}

void setupI2S_ADC() {
    i2s_config_t adc_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = true
    };
    i2s_driver_install(I2S_NUM_1, &adc_config, 0, NULL);
    i2s_pin_config_t adc_pins = {
        .bck_io_num = PIN_ADC_BCK,
        .ws_io_num = PIN_ADC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_ADC_DATA
    };
    i2s_set_pin(I2S_NUM_1, &adc_pins);
}

// ==========================================
// AUDIO CALLBACKS (BLUETOOTH)
// ==========================================

// [TX MODE] Source Callback - Bypasses Master DSP
int32_t bt_source_data_callback(Frame *data, int32_t frame_count) {
    size_t bytes_read;
    int32_t tempBuffer[frame_count * 2];

    // Read from ADC (I2S_NUM_1)
    i2s_read(I2S_NUM_1, tempBuffer, frame_count * 8, &bytes_read, portMAX_DELAY);

    for (int i=0; i < frame_count; i++) {
        StereoSample s;
        s.l = tempBuffer[i*2];
        s.r = tempBuffer[i*2+1];

        // [LOGIC] Only apply RIAA/Dolby if we are actually in AUX mode.
        // If we are in Radio mode, the signal is already Line Level.
        if (currentMode == MODE_AUX) {
            s = dsp.processAuxPreamp(s);
        }
        
        // [BYPASS] No processMasterChain here. 
        // Signal goes straight to Headphones without EQ/Loudness.

        // Update VU
        int lPeak = abs(s.l >> 23);
        int rPeak = abs(s.r >> 23);
        if(lPeak > vuLeft) vuLeft = lPeak; else vuLeft *= 0.9;
        if(rPeak > vuRight) vuRight = rPeak; else vuRight *= 0.9;

        // Write to Frame (16-bit)
        data[i].channel1 = s.l >> 16;
        data[i].channel2 = s.r >> 16;
    }
    return frame_count;
}

// [RX MODE] Metadata
void bt_volume_callback(int vol) {
    // Map BT volume (0-127) to your system volume (0-30)
    int newVol = map(vol, 0, 127, 0, 30);
    if (newVol != volume) {
        volume = newVol;
        dsp.setVolume(volume);
        preferences.putInt("vol", volume); // Optional: Save to memory
    }
}
void bt_metadata_callback(uint8_t id, const uint8_t *text) {
    if (id == 0x1) btTitle = (const char*)text;
    else if (id == 0x2) btArtist = (const char*)text;
    if (btTitle == "") btTitle = "Connected";
}

// [RX MODE] Sink Callback
void bt_data_callback(const uint8_t *data, uint32_t len) {
    size_t bytes_written;
    int16_t* samples = (int16_t*)data;
    uint32_t sample_count = len / 4; 

    for(int i=0; i < sample_count; i++) {
        StereoSample s;
        s.l = ((int32_t)samples[i*2]) << 16;
        s.r = ((int32_t)samples[i*2+1]) << 16;
        
        s = dsp.processMasterChain(s);

        int lPeak = abs(s.l >> 23);
        int rPeak = abs(s.r >> 23);
        if(lPeak > vuLeft) vuLeft = lPeak; else vuLeft *= 0.9;
        if(rPeak > vuRight) vuRight = rPeak; else vuRight *= 0.9;

        // Output to DAC
        int32_t outFrame[2] = {s.l, s.r};
        i2s_write(I2S_NUM_0, outFrame, 8, &bytes_written, portMAX_DELAY);
    }
}

// ==========================================
// AUDIO LOOPS (ANALOG & GEN)
// ==========================================
void handleAnalogLoop() {
    // If in TX mode, audio is handled by the BT Callback above, 
    // NOT by this loop. We return early to avoid I2S conflicts.
    if (isTxMode) return; 

    size_t bytes_read, bytes_written;
    int32_t i2s_buffer[64 * 2];

    i2s_read(I2S_NUM_1, i2s_buffer, sizeof(i2s_buffer), &bytes_read, 0);

    if (bytes_read > 0) {
        int samples = bytes_read / 8;
        for (int i=0; i<samples; i++) {
            StereoSample s;
            s.l = i2s_buffer[i*2];
            s.r = i2s_buffer[i*2+1];
            s = dsp.processAuxPreamp(s);
            s = dsp.processMasterChain(s);

            int lPeak = abs(s.l >> 23);
            int rPeak = abs(s.r >> 23);
            if(lPeak > vuLeft) vuLeft = lPeak; else vuLeft *= 0.9;
            if(rPeak > vuRight) vuRight = rPeak; else vuRight *= 0.9;

            i2s_buffer[i*2] = s.l;
            i2s_buffer[i*2+1] = s.r;
        }
        i2s_write(I2S_NUM_0, i2s_buffer, bytes_read, &bytes_written, portMAX_DELAY);
    }
}

void handleGenLoop() {
    size_t bytes_written;
    int32_t samples[64 * 2];
    for (int i = 0; i < 64; i++) {
        float sampleVal = 0;
        if (genSignalType == 0) { 
             sampleVal = sin(currentPhase) * 0.5;
             currentPhase += 2 * PI * genFreqStart / 44100.0;
             if(currentPhase > 2*PI) currentPhase -= 2*PI;
        }
        else if (genSignalType == 1) sampleVal = ((float)random(-1000, 1000) / 1000.0) * 0.5;
        else if (genSignalType == 2) sampleVal = generatePinkNoise() * 0.5;

        StereoSample s;
        s.l = (int32_t)(sampleVal * 2147483647.0);
        s.r = s.l;
        s = dsp.processMasterChain(s);
        samples[i*2] = s.l;
        samples[i*2+1] = s.r;
    }
    i2s_write(I2S_NUM_0, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
}

// ==========================================
// MODE SWITCHING LOGIC (REBOOT SAFE)
// ==========================================
void switchMode(OperationMode newMode) {
    if (currentMode == newMode && millis() > 5000) return;

    // --- TX MODE LOGIC (Transmitter) ---
    if (isTxMode) {
        if (newMode == MODE_BT) newMode = MODE_AUX; // Can't be BT RX in TX mode

        currentMode = newMode;
        preferences.putInt("last_mode", (int)currentMode);

        if (newMode == MODE_RADIO) {
            digitalWrite(PIN_RELAY_SOURCE, HIGH);
            radio.begin(PIN_I2C_SDA, PIN_I2C_SCL);
            buttons.setContext(CTX_RADIO);
        } else {
            digitalWrite(PIN_RELAY_SOURCE, LOW);
            buttons.setContext(CTX_TX); 
            if(newMode == MODE_AUX) {
                 if(wifiActive) { WiFi.softAPdisconnect(true); wifiActive=false; }
            }
        }

        // Install ADC ONLY (To read input). No DAC installed = Wired Mute.
        if (newMode != MODE_GEN) {
             setupI2S_ADC();
             i2s_start(I2S_NUM_1);
        }

        if (!bt.isTransmitting()) {
            bt.startTX(bt_source_data_callback);
        }
        return; 
    }

    // --- RX MODE LOGIC (Receiver) ---
    
    // Stop previous
    if (currentMode == MODE_BT) {
        bt.stop();
        delay(200);
    } else if (currentMode == MODE_RADIO) {
         radio.stop();
         digitalWrite(PIN_RELAY_SOURCE, LOW);
    }
    
    // Uninstall drivers to ensure clean switch
    i2s_driver_uninstall(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_1);

    currentMode = newMode;
    preferences.putInt("last_mode", (int)currentMode);

    if (newMode == MODE_BT) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        buttons.setContext(CTX_BT);
        bt.startRX(bt_data_callback, bt_metadata_callback, bt_volume_callback);

    } else if (newMode == MODE_RADIO) {
        digitalWrite(PIN_RELAY_SOURCE, HIGH);
        buttons.setContext(CTX_RADIO);
        setupI2S_DAC(); // Wired Sound ON
        setupI2S_ADC();
        i2s_start(I2S_NUM_0);
        i2s_start(I2S_NUM_1);
        radio.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    } else if (newMode == MODE_AUX) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        buttons.setContext(CTX_AUX);
        setupI2S_DAC(); // Wired Sound ON
        setupI2S_ADC();
        i2s_start(I2S_NUM_0);
        i2s_start(I2S_NUM_1);
        if (wifiActive) {
            WiFi.softAPdisconnect(true);
            wifiActive = false;
        }

    } else if (newMode == MODE_GEN) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        setupI2S_DAC(); // Wired Sound ON
        i2s_start(I2S_NUM_0);
        sweepStartTime = millis();
    }
}

// ==========================================
// BUTTON ACTIONS
// ==========================================

void actionVolUp() {
    if (volume < 30) volume++;
    dsp.setVolume(volume);
    if(currentMode == MODE_BT && !isTxMode) bt.setVolume(volume * 4);
    preferences.putInt("vol", volume);
}
void actionVolDown() {
    if (volume > 0) volume--;
    dsp.setVolume(volume);
    if(currentMode == MODE_BT && !isTxMode) bt.setVolume(volume * 4);
    preferences.putInt("vol", volume);
}
void actionVolRapidUp() { actionVolUp(); }
void actionVolRapidDown() { actionVolDown(); }

void actionToggleWiFi() {
    if (currentMode == MODE_AUX) return;
    wifiActive = !wifiActive;
    if (wifiActive) {
        WiFi.softAP(wifiSSID, wifiPass);
        initWebServer();
    } else {
        WiFi.softAPdisconnect(true);
    }
}

// [UPDATED] Smart Cycle: Avoids BT Receiver if we are transmitting
void actionCycleSource() {
    if (isTxMode) {
        // Toggle only Radio <-> Aux
        if (currentMode == MODE_RADIO) switchMode(MODE_AUX);
        else switchMode(MODE_RADIO); 
    } else {
        // Standard Cycle
        if (currentMode == MODE_BT) switchMode(MODE_RADIO);
        else if (currentMode == MODE_RADIO) switchMode(MODE_AUX);
        else switchMode(MODE_BT);
    }
}

// [UPDATED] Toggle TX Mode -> Save -> Reboot
void actionToggleTxMode() {
    // 1. Toggle State
    bool newState = !isTxMode;
    preferences.putBool("tx_mode", newState);
    
    // Safety: If forcing OFF from BT RX, next boot must be valid
    if (currentMode == MODE_BT) {
        preferences.putInt("last_mode", (int)MODE_AUX);
    }

    // 2. Notify User
    ui.screenLoading(newState ? "Rebooting to TX..." : "Rebooting to RX...");
    
    // 3. Reboot
    delay(1000);
    ESP.restart();
}

// --- RADIO ACTIONS ---
void actionRadioShowMemories() {
    radioShowMemories = true;
    radioCursor = 1;
    buttons.setContext(CTX_RADIO_MEM);
}
void actionRadioCursorMove() {
    radioCursor++;
    if (radioCursor > 8) radioCursor = 1;
}
void actionRadioOverwriteMem() {
    radio.saveMemory(radioCursor);
    radioShowMemories = false;
    buttons.setContext(CTX_RADIO);
}
void actionRadioActivateMem() {
    radio.loadMemory(radioCursor);
    radioShowMemories = false;
    buttons.setContext(CTX_RADIO);
}
void actionRadioSeekUp() { radio.seekUp(); }
void actionRadioSeekDown() { radio.seekDown(); }

// --- AUX ACTIONS ---
void actionAuxCycleFilters() {
    dsp.preampMode++;
    if (dsp.preampMode > 4) dsp.preampMode = 0;
}
void actionAuxMute() {
    static int savedVol = 15;
    if (volume > 0) { savedVol = volume; volume = 0; }
    else { volume = savedVol; }
    dsp.setVolume(volume);
}

// --- COMMON DSP ---
void actionToggleExpander() {
    dsp.stereoExpand = !dsp.stereoExpand;
    preferences.putBool("expand", dsp.stereoExpand);
}
void actionToggleLoudness() {
    dsp.loudnessEnabled = !dsp.loudnessEnabled;
    preferences.putBool("loud", dsp.loudnessEnabled);
}
void actionBtPairing() {
    if (currentMode == MODE_BT && !isTxMode) bt.disconnectRX();
}


// ==========================================
// UI UPDATE
// ==========================================
void updateDisplay() {
    if (millis() - lastDisplayUpdate < 100) return;
    lastDisplayUpdate = millis();
    
    int vuL = map(vuLeft, 0, 15000, 0, 100);
    int vuR = map(vuRight, 0, 15000, 0, 100);
    if(vuL>100) vuL=100; if(vuR>100) vuR=100;
    vuLeft *= 0.8; vuRight *= 0.8;

    if (currentMode == MODE_BT) {
        String track = btArtist + " - " + btTitle;
        if (btTitle == "") track = "Connected";
        ui.screenBT(dsp.loudnessEnabled, dsp.stereoExpand, volume,
                    "Pixel", track, vuL, vuR);
    }
    else if (currentMode == MODE_RADIO) {
        if (radioShowMemories) {
            String mems[8];
            for(int i=0; i<8; i++) mems[i] = radio.getMemoryLabel(i+1);
            ui.screenMemories(mems, radioCursor);
        } else {
            ui.screenRadio(dsp.loudnessEnabled, dsp.stereoExpand, volume,
                           radio.getFrequency(), 0, radio.getRDSName(), radio.getRDSText(),
                           radio.isStereo(), vuL, vuR);
        }
    }
    else if (currentMode == MODE_AUX) {
        ui.screenAux(dsp.loudnessEnabled, dsp.stereoExpand, volume,
                     (dsp.preampMode == 1), dsp.preampMode, false, 100.0,
                     vuL, vuR, isTxMode);
    }
    else if (currentMode == MODE_GEN) {
        ui.screenAux(false, false, volume, false, 0, false, 0, vuL, vuR, false);
    }
}


// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    preferences.begin("espdsp", false);

    // 1. READ TX MODE PREFERENCE
    isTxMode = preferences.getBool("tx_mode", false);

    // Hardware Init
    pinMode(PIN_RELAY_SOURCE, OUTPUT);
    digitalWrite(PIN_RELAY_SOURCE, LOW);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    ui.begin();
    ui.screenLoading(isTxMode ? "TX Mode (Headphones)" : "RX Mode (Speaker)");

    buttons.begin();
    
    String btName = preferences.getString("bt_name", "ESPDSP-Receiver");
    bt.init(btName);

    // Load Settings
    volume = preferences.getInt("vol", 15);
    dsp.setVolume(volume);
    dsp.loudnessEnabled = preferences.getBool("loud", false);
    dsp.stereoExpand = preferences.getBool("expand", false);

    // Start Initial Mode
    int savedMode = preferences.getInt("last_mode", (int)MODE_BT);
    delay(1000);
    switchMode((OperationMode)savedMode);
}

void loop() {
    buttons.update();

    if (currentMode == MODE_RADIO && !radioShowMemories) {
        radio.loop();
    }

    if (currentMode == MODE_AUX || currentMode == MODE_RADIO) {
        handleAnalogLoop();
    }
    else if (currentMode == MODE_GEN) {
        handleGenLoop();
    }

    if (genActive && currentMode != MODE_GEN) {
        switchMode(MODE_GEN);
    }
    else if (!genActive && currentMode == MODE_GEN) {
        switchMode(MODE_BT);
    }

    updateDisplay();
    if (wifiActive) server.handleClient();
}