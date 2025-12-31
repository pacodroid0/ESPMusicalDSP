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

// --- 1. MISSING INCLUDE RESTORED ---
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
// [FIX] Added MODE_GEN back
enum OperationMode { MODE_BT, MODE_RADIO, MODE_AUX, MODE_GEN };
OperationMode currentMode = MODE_BT;

int volume = 15;
bool wifiActive = false;
bool isTxMode = false;

// --- 2. GENERATOR GLOBALS RESTORED ---
int genSignalType = 0; // 0=Sine, 1=White, 2=Pink, 3=Sweep
bool genActive = false;
float genFreqStart = 440.0;
float genFreqEnd = 440.0;
float genPeriod = 10.0;
unsigned long sweepStartTime = 0;
double currentPhase = 0;

// Display State
unsigned long lastDisplayUpdate = 0;
String btTitle = "";
String btArtist = "";
int vuLeft = 0;
int vuRight = 0;

// Radio Memory UI State
bool radioShowMemories = false;
int radioCursor = 1;

// --- 3. WEB SERVER INCLUDE (Must be after Globals) ---
#include "web_server.h"

// --- FORWARD DECLARATIONS ---
void bt_data_callback(const uint8_t *data, uint32_t len);
void bt_metadata_callback(uint8_t id, const uint8_t *text);
int32_t bt_source_data_callback(Frame *data, int32_t len);
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
// AUDIO CALLBACKS
// ==========================================

int32_t bt_source_data_callback(Frame *data, int32_t frame_count) {
    size_t bytes_read;
    // 'frame_count' is the number of stereo samples requested.
    // Each frame is 2x int16_t (4 bytes).
    
    // We need a temp buffer for 32-bit I2S data (Left + Right)
    int32_t tempBuffer[frame_count * 2];

    // Read from ADC (I2S_NUM_1)
    // We read frame_count * 8 bytes (because we read 2x 32-bit ints per frame)
    i2s_read(I2S_NUM_1, tempBuffer, frame_count * 8, &bytes_read, portMAX_DELAY);

    for (int i=0; i < frame_count; i++) {
        StereoSample s;
        // Read 32-bit data from I2S
        s.l = tempBuffer[i*2];
        s.r = tempBuffer[i*2+1];

        // Process DSP
        s = dsp.processAuxPreamp(s);
        s = dsp.processMasterChain(s);

        // Update VU Meter
        int lPeak = abs(s.l >> 23);
        int rPeak = abs(s.r >> 23);
        if(lPeak > vuLeft) vuLeft = lPeak; else vuLeft *= 0.9;
        if(rPeak > vuRight) vuRight = rPeak; else vuRight *= 0.9;

        // [FIX] Write directly to the Frame struct (16-bit)
        // Shift down by 16 to convert 32-bit internal audio to 16-bit BT audio
        data[i].channel1 = s.l >> 16;
        data[i].channel2 = s.r >> 16;
    }
    
    // Return the number of frames provided
    return frame_count; 
}

// [FIXED] Updated signature and logic for new A2DP library
void bt_metadata_callback(uint8_t id, const uint8_t *text) {
    // 0x1 = Title, 0x2 = Artist, 0x4 = Album (Standard ESP-IDF AVRC IDs)
    if (id == 0x1) { 
        btTitle = (const char*)text;
    } 
    else if (id == 0x2) {
        btArtist = (const char*)text;
    }
    // Optional: Reset title if empty
    if (btTitle == "") btTitle = "Connected";
}
// [FIXED] Ensure signature matches header exactly
void bt_data_callback(const uint8_t *data, uint32_t len) {
    size_t bytes_written;
    int16_t* samples = (int16_t*)data;
    uint32_t sample_count = len / 4;

    for(int i=0; i < sample_count; i++) {
        StereoSample s;
        // Shift left to convert 16-bit BT audio to 32-bit DSP audio
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
int32_t bt_source_data_callback(uint8_t *data, int32_t len) {
    size_t bytes_read;
    int frames = len / 4;
    int32_t tempBuffer[frames * 2];

    i2s_read(I2S_NUM_1, tempBuffer, frames * 8, &bytes_read, portMAX_DELAY);
    int16_t* out16 = (int16_t*)data;

    for (int i=0; i<frames; i++) {
        StereoSample s;
        s.l = tempBuffer[i*2];
        s.r = tempBuffer[i*2+1];
        s = dsp.processAuxPreamp(s);
        s = dsp.processMasterChain(s);

        int lPeak = abs(s.l >> 23);
        int rPeak = abs(s.r >> 23);
        if(lPeak > vuLeft) vuLeft = lPeak; else vuLeft *= 0.9;
        if(rPeak > vuRight) vuRight = rPeak; else vuRight *= 0.9;

        out16[i*2] = s.l >> 16;
        out16[i*2+1] = s.r >> 16;
    }
    return len;
}

// ==========================================
// AUDIO LOOPS (ANALOG & GEN)
// ==========================================
void handleAnalogLoop() {
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

// 4. RESTORED GENERATOR LOOP
void handleGenLoop() {
    size_t bytes_written;
    int32_t samples[64 * 2];

    for (int i = 0; i < 64; i++) {
        float sampleVal = 0;

        // PNoise library usage or simple generation
        if (genSignalType == 0) { // Sine
             sampleVal = sin(currentPhase) * 0.5;
             currentPhase += 2 * PI * 440.0 / 44100.0;
             if(currentPhase > 2*PI) currentPhase -= 2*PI;
        }
        else if (genSignalType == 1) { // White
             sampleVal = ((float)random(-1000, 1000) / 1000.0) * 0.5;
        }
        else if (genSignalType == 2) { // Pink
             sampleVal = generatePinkNoise() * 0.5;
        }

        // Apply Volume DSP
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
// MODE SWITCHING LOGIC
// ==========================================
void switchMode(OperationMode newMode) {
    if (currentMode == newMode) return;

    // Stop Logic
    if (currentMode == MODE_BT) {
        bt.stop();
        delay(200);
        setupI2S_DAC();
    } else if (currentMode == MODE_RADIO) {
         radio.stop();
         digitalWrite(PIN_RELAY_SOURCE, LOW);
    }

    currentMode = newMode;
    preferences.putInt("last_mode", (int)currentMode);

    // Start Logic
    if (newMode == MODE_BT) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        buttons.setContext(CTX_BT);
        bt.startRX(bt_data_callback, bt_metadata_callback);

    } else if (newMode == MODE_RADIO) {
        digitalWrite(PIN_RELAY_SOURCE, HIGH);
        buttons.setContext(CTX_RADIO);
        setupI2S_DAC();
        setupI2S_ADC();
        i2s_start(I2S_NUM_0);
        i2s_start(I2S_NUM_1);
        radio.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    } else if (newMode == MODE_AUX) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        buttons.setContext(CTX_AUX);
        setupI2S_DAC();
        setupI2S_ADC();
        i2s_start(I2S_NUM_0);
        i2s_start(I2S_NUM_1);
        if (wifiActive) {
            WiFi.softAPdisconnect(true);
            wifiActive = false;
        }
    }
    // [FIX] Handling GEN Mode start
    else if (newMode == MODE_GEN) {
        digitalWrite(PIN_RELAY_SOURCE, LOW);
        setupI2S_DAC(); // Only DAC needed
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
    if(currentMode == MODE_BT) bt.setVolume(volume * 4);
    preferences.putInt("vol", volume);
}
void actionVolDown() {
    if (volume > 0) volume--;
    dsp.setVolume(volume);
    if(currentMode == MODE_BT) bt.setVolume(volume * 4);
    preferences.putInt("vol", volume);
}
void actionVolRapidUp() { actionVolUp(); }
void actionVolRapidDown() { actionVolDown(); }

void actionToggleWiFi() {
    if (currentMode == MODE_AUX) return;
    wifiActive = !wifiActive;
    if (wifiActive) {
        WiFi.softAP(wifiSSID, wifiPass);
        // [FIX] Call init from web_server.h
        initWebServer();
    } else {
        WiFi.softAPdisconnect(true);
    }
}

void actionCycleSource() {
    if (currentMode == MODE_BT) switchMode(MODE_RADIO);
    else if (currentMode == MODE_RADIO) switchMode(MODE_AUX);
    else switchMode(MODE_BT);
    // Note: GEN mode is not in the button cycle, only via Web
}

void actionToggleTxMode() {
    if (currentMode == MODE_BT || currentMode == MODE_GEN) return;
    isTxMode = !isTxMode;
    if (isTxMode) {
        buttons.setContext(CTX_TX);
        bt.startTX(bt_source_data_callback);
    } else {
        bt.stop();
        buttons.setContext(currentMode == MODE_RADIO ? CTX_RADIO : CTX_AUX);
    }
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
    if (currentMode == MODE_BT) bt.disconnectRX();
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
    // [FIX] Display for Gen Mode
    else if (currentMode == MODE_GEN) {
        ui.screenAux(false, false, volume, false, 0, false, 0, vuL, vuR, false);
        // Reuse Aux screen or create simple "GENERATOR" screen in displayinfo
    }
}


// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    preferences.begin("espdsp", false);

    // 1. Hardware Init
    pinMode(PIN_RELAY_SOURCE, OUTPUT);
    digitalWrite(PIN_RELAY_SOURCE, LOW);

    // I2C Init
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    ui.begin();
    ui.screenLoading("v3.0 Ultimate");

    // 2. Buttons
    buttons.begin();

    // 3. Bluetooth Init
    String btName = preferences.getString("bt_name", "ESPDSP-Receiver");
    bt.init(btName);

    // 4. Load Settings
    volume = preferences.getInt("vol", 15);
    dsp.setVolume(volume);
    dsp.loudnessEnabled = preferences.getBool("loud", false);
    dsp.stereoExpand = preferences.getBool("expand", false);

    // 5. Start Initial Mode
    int savedMode = preferences.getInt("last_mode", (int)MODE_BT);
    delay(1000);
    switchMode((OperationMode)savedMode);
}

void loop() {
    // 1. Inputs
    buttons.update();

    // 2. Radio RDS Background
    if (currentMode == MODE_RADIO && !radioShowMemories) {
        radio.loop();
    }

    // 3. Audio Loop
    if (currentMode == MODE_AUX || currentMode == MODE_RADIO) {
        handleAnalogLoop();
    }
    else if (currentMode == MODE_GEN) {
        handleGenLoop(); // [FIX] Added back
    }

    // 4. Auto-switch to Gen mode if toggled via Web UI
    // (Variables set by web_server.h callbacks)
    if (genActive && currentMode != MODE_GEN) {
        switchMode(MODE_GEN);
    }
    else if (!genActive && currentMode == MODE_GEN) {
        switchMode(MODE_BT);
    }

    // 5. UI
    updateDisplay();

    // 6. WiFi
    if (wifiActive) server.handleClient();
}
