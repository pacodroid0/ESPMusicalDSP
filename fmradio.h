#ifndef FMRADIO_H
#define FMRADIO_H

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <RDA5807.h> // REQUIRED LIBRARY: "PU2CLR RDA5807" by Ricardo Lima Caratti

// RDS Polling Interval
#define RDS_POLL_MS 50

class RadioManager {
private:
    RDA5807 rx;
    Preferences prefs;

    // RDS State
    char rdsStationName[32]; // Buffer for Station Name
    char rdsText[65];        // Buffer for Radio Text (64 chars + null)
    unsigned long lastRDSPoll = 0;

    // Tuning State
    bool isSeeking = false;
    float currentFreq = 87.50;

public:
    bool rdsAvailable = false;

    void begin(int sda, int scl) {
        // Init I2C
        Wire.begin(sda, scl);

        // Radio Setup
        rx.setup(); 
        rx.setVolume(15); // Fixed high volume (controlled via DSP later)
        rx.setMono(false);
        rx.setRDS(true);
        rx.setRdsFifo(true);

        // Load Last Frequency
        prefs.begin("radio_mem", false); 
        int lastF = prefs.getInt("last_freq", 9800); 
        setFrequency(lastF / 100.0f);

        // Clear Buffers
        memset(rdsStationName, 0, sizeof(rdsStationName));
        memset(rdsText, 0, sizeof(rdsText));
    }

    void stop() {
        rx.powerDown();
    }

    // --- TUNING ---

    void setFrequency(float freq) {
        // Save for next boot
        int fInt = (int)(freq * 100);
        prefs.putInt("last_freq", fInt);

        rx.setFrequency((uint16_t)(freq * 100)); 
        currentFreq = freq;
        clearRDS();
    }

    float getFrequency() {
        return rx.getFrequency() / 100.0f;
    }

    void seekUp() {
        clearRDS();
        rx.seek(RDA_SEEK_WRAP, RDA_SEEK_UP, notifySeek);
        delay(200); 
        float newF = getFrequency();
        prefs.putInt("last_freq", (int)(newF * 100));
    }

    void seekDown() {
        clearRDS();
        rx.seek(RDA_SEEK_WRAP, RDA_SEEK_DOWN, notifySeek);
        delay(200);
        float newF = getFrequency();
        prefs.putInt("last_freq", (int)(newF * 100));
    }

    // Callback required by library (must be static)
    static void notifySeek() {}

    // --- MEMORIES ---

    void saveMemory(int slot) {
        if (slot < 1 || slot > 8) return;
        char key[8];
        sprintf(key, "mem_%d", slot);
        int freqInt = (int)(getFrequency() * 100);
        prefs.putInt(key, freqInt);
    }

    void loadMemory(int slot) {
        if (slot < 1 || slot > 8) return;
        char key[8];
        sprintf(key, "mem_%d", slot);
        int freqInt = prefs.getInt(key, 0);
        if (freqInt > 6000) { 
            setFrequency(freqInt / 100.0f);
        }
    }

    String getMemoryLabel(int slot) {
        char key[8];
        sprintf(key, "mem_%d", slot);
        int f = prefs.getInt(key, 0);
        if (f == 0) return "Empty";
        return String(f / 100.0f, 2);
    }

    // --- RDS LOOP ---
    void loop() {
        if (millis() - lastRDSPoll > RDS_POLL_MS) {
            lastRDSPoll = millis();

            if (rx.getRdsReady()) {
                rdsAvailable = true;

                // [FIX 1] Correct Method for Station Name
                // Old: getRdsProgramService() -> New: getRdsStationInformation()
                char* ps = rx.getRdsStationInformation();
                if (ps && strlen(ps) > 0) {
                     strncpy(rdsStationName, ps, 31);
                     rdsStationName[31] = '\0';
                }

                // [FIX 2] Correct Method for Scrolling Text
                // Old: getRdsText() -> New: getRdsText0A()
                char* rt = rx.getRdsText0A();
                 if (rt && strlen(rt) > 0) {
                     strncpy(rdsText, rt, 64);
                     rdsText[64] = '\0';
                }
            }
        }
    }

    // Getters for Display
    String getRDSName() {
        if (strlen(rdsStationName) > 0) return String(rdsStationName);
        return "";
    }

    String getRDSText() {
         if (strlen(rdsText) > 0) return String(rdsText);
         return "";
    }

    int getRSSI() {
        return rx.getRssi(); 
    }

    bool isStereo() {
        return rx.isStereo();
    }

private:
    void clearRDS() {
        memset(rdsStationName, 0, sizeof(rdsStationName));
        memset(rdsText, 0, sizeof(rdsText));
        rdsAvailable = false;
    }
};

#endif