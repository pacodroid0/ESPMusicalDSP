#ifndef DISPLAYINFO_H
#define DISPLAYINFO_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// --- CUSTOM CHARACTERS FOR VU METER ---
// These create a smooth progress bar effect
const byte bar1[8] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
const byte bar2[8] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18};
const byte bar3[8] = {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C};
const byte bar4[8] = {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E};
const byte bar5[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}; // Full Block

class DisplayUI {
private:
    LiquidCrystal_I2C* lcd;

    // Scrolling State Variables
    String lastScrollText = "";
    int scrollPos = 0;
    unsigned long lastScrollTime = 0;
    const int scrollDelay = 400; // Speed of scrolling
    const int rowWidth = 20;

public:
    DisplayUI(LiquidCrystal_I2C* _lcd) {
        lcd = _lcd;
    }

    void begin() {
        lcd->init();
        lcd->backlight();

        // Register Custom Chars (Indices 0-4)
        lcd->createChar(0, (uint8_t*)bar1);
        lcd->createChar(1, (uint8_t*)bar2);
        lcd->createChar(2, (uint8_t*)bar3);
        lcd->createChar(3, (uint8_t*)bar4);
        lcd->createChar(4, (uint8_t*)bar5);
    }

    // ==========================================
    // HELPER: STATUS BAR (Top Row)
    // Layout: |SOURCE   LOUD WIDE 30|
    // ==========================================
    void drawStatusBar(String source, bool loud, bool wide, int vol) {
        lcd->setCursor(0, 0);

        // 1. Source (Left, padded to 8 chars)
        String s = source;
        while(s.length() < 8) s += " ";
        lcd->print(s);

        // 2. Indicators (Middle)
        if(loud) lcd->print("L "); else lcd->print("  "); // Tiny L indicator or full LOUD if space permits
        if(wide) lcd->print("W "); else lcd->print("  ");

        // 3. Volume (Right justified)
        lcd->setCursor(17, 0);
        if(vol < 10) lcd->print(" ");
        lcd->print(vol);

        // Note: Modified slightly to fit 20 chars: "AUX     L W    30"
        // To match your exact "LOUD WIDE" mockup, source name must be short.
        // Revised based on mockup:
        lcd->setCursor(0,0);
        lcd->print(source.substring(0, 7)); // Cap source len
        lcd->setCursor(8, 0);
        lcd->print(loud ? "LOUD" : "    ");
        lcd->setCursor(13, 0);
        lcd->print(wide ? "WIDE" : "    ");
        lcd->setCursor(18, 0);
        if(vol < 10) lcd->print("0");
        lcd->print(vol);
    }

    // ==========================================
    // HELPER: VU METER (Bottom Row)
    // Layout: |   ||||| LR |||||   |
    // ==========================================
    void drawVUMeter(int leftVal, int rightVal, String centerText = "LR") {
        // Map 0-100 input to 0-25 (5 chars * 5 segments)
        int lMap = map(leftVal, 0, 100, 0, 25);
        int rMap = map(rightVal, 0, 100, 0, 25);

        lcd->setCursor(0, 3);
        lcd->print("   "); // Padding

        // Draw Left Bar (Reverse direction is tricky on LCD, drawing standard L->R)
        // Ideally Left bar grows Right-to-Left, but L->R is standard.
        // Let's draw L->R for simplicity or implement custom logic for R->L.
        // Implementation: [][][][][] LR [][][][][]

        // LEFT CHANNEL (5 Chars)
        for(int i=0; i<5; i++) {
            int segs = lMap - (i*5);
            if(segs >= 5) lcd->write(4);      // Full block
            else if(segs > 0) lcd->write(segs-1); // Partial
            else lcd->print(" ");             // Empty
        }

        lcd->print(" ");
        lcd->print(centerText); // "LR" or "ST" or "MO"
        lcd->print(" ");

        // RIGHT CHANNEL (5 Chars)
        for(int i=0; i<5; i++) {
            int segs = rMap - (i*5);
            if(segs >= 5) lcd->write(4);
            else if(segs > 0) lcd->write(segs-1);
            else lcd->print(" ");
        }

        lcd->print("   ");
    }

    // ==========================================
    // HELPER: SCROLLING TEXT
    // ==========================================
    void drawScrollingText(int row, String text) {
        // Only update if text changed
        if (text != lastScrollText) {
            lastScrollText = text;
            scrollPos = 0;
            lcd->setCursor(0, row);
            String displayTxt = text;
            while(displayTxt.length() < 20) displayTxt += " "; // Pad
            lcd->print(displayTxt.substring(0, 20));
            return;
        }

        // If text fits, don't scroll
        if (text.length() <= 20) {
             lcd->setCursor(0, row);
             String displayTxt = text;
             while(displayTxt.length() < 20) displayTxt += " ";
             lcd->print(displayTxt);
             return;
        }

        // Timer for scrolling
        if (millis() - lastScrollTime > scrollDelay) {
            lastScrollTime = millis();
            scrollPos++;
            if (scrollPos > (text.length() - 20 + 4)) { // +4 pause at end
                scrollPos = 0;
            }

            lcd->setCursor(0, row);
            int start = scrollPos;
            if (start < 0) start = 0; // Handle pause at start if implemented

            String slice = text.substring(start);
            // If slice is shorter than 20 (end of scroll), pad with spaces
            while(slice.length() < 20) slice += " ";
            lcd->print(slice.substring(0, 20));
        }
    }

    // ==========================================
    // SCREEN 1: LOADING
    // ==========================================
    void screenLoading(String version) {
        lcd->clear();
        lcd->setCursor(0, 1);
        lcd->print("    MUSICAL DSP     ");
        lcd->setCursor(15, 3);
        lcd->print(version);
    }

    // ==========================================
    // SCREEN 2: BLUETOOTH
    // ==========================================
    void screenBT(bool loud, bool wide, int vol, String deviceName, String trackInfo, int vuL, int vuR) {
        drawStatusBar("BLUE", loud, wide, vol);

        lcd->setCursor(0, 1);
        if(deviceName.length() > 0) lcd->print(deviceName.substring(0, 20));
        else lcd->print("Waiting Connection..");

        drawScrollingText(2, trackInfo);

        drawVUMeter(vuL, vuR, "LR");
    }

    // ==========================================
    // SCREEN 3: RADIO
    // ==========================================
    void screenRadio(bool loud, bool wide, int vol, float freq, int memIdx, String rdsName, String signalInfo, bool stereo, int vuL, int vuR) {
        drawStatusBar("RADIO", loud, wide, vol);

        // ROW 1: |108.80 M1 VIRGIN 70S|
        lcd->setCursor(0, 1);
        lcd->print(freq, 2);

        lcd->setCursor(7, 1);
        if(memIdx > 0) {
            lcd->print("M"); lcd->print(memIdx);
        } else {
            lcd->print("  ");
        }

        lcd->setCursor(10, 1);
        if(rdsName.length() > 10) lcd->print(rdsName.substring(0, 10)); // Clip RDS name on this line
        else lcd->print(rdsName);

        // ROW 2: Scrolling or Signal
        // Mockup says: |Queen - Innuendo    | OR Signal
        drawScrollingText(2, signalInfo);

        // ROW 3: VU
        drawVUMeter(vuL, vuR, stereo ? "ST" : "MO");
    }

    // ==========================================
    // SCREEN 4: MEMORIES (Grid 2x4)
    // ==========================================
    // Layout:
    // Row 0: |1 NAME     5 NAME    |
    // Row 1: |2>NAME     6 NAME    |
    // Row 2: |3 NAME     7 NAME    |
    // Row 3: |4 NAME     8 NAME    |
    // ==========================================
    void screenMemories(String memNames[], int selIdx) {
        // No Status bar, No Title. Full grid.

        for (int i = 0; i < 8; i++) {
            // Calculate Position
            // Items 0-3 go in Col 0 (Rows 0-3)
            // Items 4-7 go in Col 10 (Rows 0-3)
            int colOffset = (i < 4) ? 0 : 10;
            int rowIdx = (i % 4);

            lcd->setCursor(colOffset, rowIdx);

            // 1. Draw Index Number (1-8)
            lcd->print(i + 1);

            // 2. Draw Cursor ">" or Space
            // selIdx is 1-based (1-8)
            if ((i + 1) == selIdx) {
                lcd->print(">");
            } else {
                lcd->print(" ");
            }

            // 3. Draw Name (Max 7 chars to fit in 10-char half-width)
            // Format: "1>NAME..." is 1+1+7 = 9 chars. One space padding at end = 10.
            String n = memNames[i];
            if (n.length() == 0) n = "Empty";

            // Pad or truncate to exactly 7 chars
            if (n.length() > 7) {
                n = n.substring(0, 7);
            }

            lcd->print(n);

            // Ensure column clean-up (padding to fill the 10-char block)
            // If name was "ABC", we printed "1 ABC", length 5. Need 5 spaces.
            // Current printed len: 1 (digit) + 1 (cursor) + n.length().
            int printedLen = 2 + n.length();
            for(int k=printedLen; k<10; k++) {
                lcd->print(" ");
            }
        }
    }

    // ==========================================
    // SCREEN 5: AUX
    // ==========================================
    void screenAux(bool loud, bool wide, int vol, bool riaa, int nrMode, bool lowPass, float gain, int vuL, int vuR, bool isTx = false) {

        if (isTx) drawStatusBar("AUX-TX", loud, wide, vol);
        else      drawStatusBar("AUX", loud, wide, vol);

        // ROW 1: |RIAA            NR-B|
        lcd->setCursor(0, 1);
        if(riaa) lcd->print("RIAA");
        else     lcd->print("    ");

        // Justify Right for NR
        String nrStr = "";
        if(nrMode == 1) nrStr = "NR-B";
        else if(nrMode == 2) nrStr = "NR-C";
        else if(nrMode == 3) nrStr = " DBX";

        lcd->setCursor(20 - nrStr.length(), 1);
        lcd->print(nrStr);

        // ROW 2: |GAIN 100%    LOWPASS| OR |BLUE OUT   CONNECTED|
        lcd->setCursor(0, 2);
        if (isTx) {
            lcd->print("BLUE OUT   ");
            // TX Status passed in "gain" var or separate? Assuming connected for now based on mockup
            lcd->print("CONNECTED");
        } else {
            lcd->print("GAIN ");
            lcd->print((int)gain); lcd->print("%");

            if(lowPass) {
                 String lp = "LOWPASS";
                 lcd->setCursor(20 - lp.length(), 2);
                 lcd->print(lp);
            }
        }

        // ROW 3: VU
        drawVUMeter(vuL, vuR, isTx ? "MO" : "LR"); // TX usually Mono or Joint
    }
};

#endif
