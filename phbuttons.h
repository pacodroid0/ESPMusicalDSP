#ifndef PHBUTTONS_H
#define PHBUTTONS_H

#include <Arduino.h>
#include "pindef.h"

// --- CONSTANTS FOR TIMING ---
#define DEBOUNCE_MS      50
#define LONG_PRESS_MS    2000  // As per your table (> 2 sec)
#define DOUBLE_CLICK_MS  400
#define RAPID_VOL_MS     200
#define MEMORY_WAIT_MS   2000  // The "Wait 2 sec" logic

// --- ENUMS FOR CONTEXT ---
enum ButtonContext {
    CTX_BT,
    CTX_RADIO,
    CTX_RADIO_MEM, // Special context for Memory Screen
    CTX_AUX,
    CTX_TX         // Bluetooth Transmitter
};

// --- EXTERNAL ACTION CALLBACKS ---
// You must implement these void functions in your main.ino
extern void actionVolUp();
extern void actionVolDown();
extern void actionVolRapidUp();
extern void actionVolRapidDown();
extern void actionToggleWiFi();

extern void actionCycleSource();
extern void actionToggleTxMode();

extern void actionRadioShowMemories();
extern void actionRadioCursorMove();
extern void actionRadioOverwriteMem();
extern void actionRadioActivateMem(); // The "Wait" action
extern void actionRadioSeekUp();
extern void actionRadioSeekDown();

extern void actionAuxCycleFilters();
extern void actionAuxMute();

extern void actionToggleExpander();
extern void actionToggleLoudness();
extern void actionBtPairing();

// ==========================================
// CLASS: SINGLE BUTTON HANDLER
// ==========================================
class SmartButton {
private:
    uint8_t pin;
    bool state = HIGH;
    bool lastState = HIGH;
    unsigned long pressStartTime = 0;
    bool isLongPressed = false;
    bool waitingForDoubleClick = false;
    unsigned long lastClickTime = 0;

public:
    // Events flags
    bool justPressed = false;
    bool justReleased = false;
    bool justLongPressed = false;
    bool justDoubleClicked = false;
    bool isHeld = false; // For rapid volume
    unsigned long lastChangeTime = 0;

    SmartButton(uint8_t p) : pin(p) {}

    void begin() {
        pinMode(pin, INPUT_PULLUP);
    }

    void update() {
        // Reset single-shot events
        justPressed = false;
        justReleased = false;
        justLongPressed = false;
        justDoubleClicked = false;

        bool reading = digitalRead(pin);

        // Debounce
        if (reading != lastState) {
            lastChangeTime = millis();
        }

        if ((millis() - lastChangeTime) > DEBOUNCE_MS) {
            if (reading != state) {
                state = reading;

                // LOW = PRESSED (Input Pullup)
                if (state == LOW) {
                    pressStartTime = millis();
                    isLongPressed = false;
                    isHeld = true;
                } else {
                    // RELEASED
                    isHeld = false;
                    unsigned long duration = millis() - pressStartTime;

                    if (!isLongPressed) {
                        // It was a short press
                        if (waitingForDoubleClick && (millis() - lastClickTime < DOUBLE_CLICK_MS)) {
                            justDoubleClicked = true;
                            waitingForDoubleClick = false; // Reset
                        } else {
                            justReleased = true; // Potentially a single click
                            waitingForDoubleClick = true;
                            lastClickTime = millis();
                        }
                    }
                }
            }
        }

        // Long Press Logic
        if (state == LOW && !isLongPressed && (millis() - pressStartTime > LONG_PRESS_MS)) {
            justLongPressed = true;
            isLongPressed = true;
            waitingForDoubleClick = false; // Cancel double click if held long
        }

        lastState = reading;
    }

    // Check if we have a pending single click that timed out (so it's definitely single)
    bool hasSingleClickPending() {
        if (waitingForDoubleClick && (millis() - lastClickTime > DOUBLE_CLICK_MS)) {
            waitingForDoubleClick = false;
            return true;
        }
        return false;
    }

    // Force clear double click state (useful if handled)
    void clearPending() { waitingForDoubleClick = false; }
};

// ==========================================
// CLASS: BUTTON LOGIC MANAGER
// ==========================================
class ButtonManager {
private:
    SmartButton btnVolUp;
    SmartButton btnVolDown;
    SmartButton btnSource;
    SmartButton btnPreset;
    SmartButton btnPair;

    ButtonContext currentContext = CTX_BT;

    // Timer for "Wait 2 sec" logic in Radio Memory
    unsigned long lastRadioMemActivity = 0;
    bool radioMemWaitActive = false;

    // Timer for Rapid Volume
    unsigned long lastRapidVolTime = 0;

public:
    ButtonManager() :
        btnVolUp(BTN_VOL_UP),
        btnVolDown(BTN_VOL_DOWN),
        btnSource(BTN_SOURCE),
        btnPreset(BTN_PRESET),
        btnPair(BTN_PAIR) {}

    void begin() {
        btnVolUp.begin();
        btnVolDown.begin();
        btnSource.begin();
        btnPreset.begin();
        btnPair.begin();
    }

    void setContext(ButtonContext ctx) {
        // If entering MEM context, start timer
        if (ctx == CTX_RADIO_MEM && currentContext != CTX_RADIO_MEM) {
            lastRadioMemActivity = millis();
            radioMemWaitActive = true;
        }
        currentContext = ctx;
    }

    void update() {
        // 1. Read Raw States
        btnVolUp.update();
        btnVolDown.update();
        btnSource.update();
        btnPreset.update();
        btnPair.update();

        // 2. Handle COMBO (WiFi) - Priority High
        // Logic: If both held > 2 sec
        if (btnVolUp.isHeld && btnVolDown.isHeld) {
            if (btnVolUp.justLongPressed || btnVolDown.justLongPressed) {
                actionToggleWiFi();
                // Consume events so individual buttons don't fire
                btnVolUp.clearPending(); btnVolDown.clearPending();
                return;
            }
        }

        // 3. Handle VOLUME (Always Active)
        // Single Clicks
        if (btnVolUp.hasSingleClickPending()) actionVolUp();
        if (btnVolDown.hasSingleClickPending()) actionVolDown();

        // Rapid Volume (Held > LongPress threshold)
        if (btnVolUp.isHeld && millis() - lastRapidVolTime > RAPID_VOL_MS && btnVolUp.justLongPressed == false) {
             // Only start rapid AFTER the initial long press trigger or delay
             // Simplified: If held long enough, pulse events
             // Note: User table says "Long Press > 2s = Volume Rapido".
             // We'll treat "isLongPressed" state as enabling rapid fire.
             if(millis() - btnVolUp.lastChangeTime > LONG_PRESS_MS) {
                 actionVolRapidUp();
                 lastRapidVolTime = millis();
             }
        }
        if (btnVolDown.isHeld && millis() - lastRapidVolTime > RAPID_VOL_MS && btnVolDown.justLongPressed == false) {
             if(millis() - btnVolDown.lastChangeTime > LONG_PRESS_MS) {
                 actionVolRapidDown();
                 lastRapidVolTime = millis();
             }
        }

        // 4. Handle SOURCE (Always Active)
        if (btnSource.hasSingleClickPending()) actionCycleSource();
        if (btnSource.justLongPressed) actionToggleTxMode();

        // 5. Handle CONTEXT SPECIFIC LOGIC
        switch (currentContext) {

            // --- CONTEXT: RADIO ---
            case CTX_RADIO:
                // PRESET
                if (btnPreset.hasSingleClickPending()) actionRadioShowMemories(); // Enter Mem Screen
                if (btnPreset.justLongPressed) actionToggleExpander();
                if (btnPreset.justDoubleClicked) actionToggleLoudness();

                // PAIR
                if (btnPair.hasSingleClickPending()) actionRadioSeekUp();
                if (btnPair.justLongPressed) actionRadioSeekDown();
                break;

            // --- CONTEXT: RADIO MEMORY SCREEN ---
            case CTX_RADIO_MEM:
                // PRESET
                if (btnPreset.hasSingleClickPending()) {
                    actionRadioCursorMove();
                    lastRadioMemActivity = millis(); // Reset Wait Timer
                    radioMemWaitActive = true;
                }
                if (btnPreset.justLongPressed) {
                    actionRadioOverwriteMem();
                    radioMemWaitActive = false; // Action taken, stop waiting
                }

                // WAIT LOGIC (2 sec timeout)
                if (radioMemWaitActive && (millis() - lastRadioMemActivity > MEMORY_WAIT_MS)) {
                    actionRadioActivateMem();
                    radioMemWaitActive = false;
                }
                break;

            // --- CONTEXT: AUX ---
            case CTX_AUX:
                // PRESET
                if (btnPreset.hasSingleClickPending()) actionAuxCycleFilters();
                if (btnPreset.justLongPressed) actionToggleExpander();
                if (btnPreset.justDoubleClicked) actionToggleLoudness();

                // PAIR
                if (btnPair.hasSingleClickPending()) actionAuxMute();
                break;

            // --- CONTEXT: BLUETOOTH ---
            case CTX_BT:
                // PRESET
                // Table: Short=[Empty], Long=Expander, Double=Loudness
                if (btnPreset.justLongPressed) actionToggleExpander();
                if (btnPreset.justDoubleClicked) actionToggleLoudness();

                // PAIR
                if (btnPair.hasSingleClickPending()) actionBtPairing();
                break;

             // --- CONTEXT: TX MODE ---
            case CTX_TX:
                 // Minimal controls in TX mode (Volume usually works, maybe Source to exit)
                 break;
        }
    }
};

#endif
