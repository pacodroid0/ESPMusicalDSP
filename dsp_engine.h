#ifndef DSP_ENGINE_H
#define DSP_ENGINE_H

#include <math.h>
#include <vector>
#include <Arduino.h>

// --- INCLUDES ---
#include "loud.h"
#include "stereoexpander.h"

// 32-bit audio handling
struct StereoSample {
    int32_t l;
    int32_t r;
};

// ==========================================================
// BIQUAD FILTER CLASS
// (Defined here so vintage.h can use it)
// ==========================================================
class Biquad {
public:
    // Coefficients
    float b0=1.0, b1=0.0, b2=0.0, a1=0.0, a2=0.0;

    // State variables (History)
    float x1_l=0, x2_l=0, y1_l=0, y2_l=0;
    float x1_r=0, x2_r=0, y1_r=0, y2_r=0;

    // Reset history to prevent pops on change
    void resetState() {
        x1_l=0; x2_l=0; y1_l=0; y2_l=0;
        x1_r=0; x2_r=0; y1_r=0; y2_r=0;
    }

    // Filter Type 1: Peaking EQ
    void setPeaking(float centerFreq, float gaindB, float Q = 1.0) {
        float sampleRate = 44100.0;
        float w0 = 2 * PI * centerFreq / sampleRate;
        float alpha = sin(w0) / (2 * Q);
        float A = pow(10, gaindB / 40.0);
        float cosw0 = cos(w0);

        float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = (-2 * cosw0) / a0; b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cosw0) / a0; a2 = (1 - alpha / A) / a0;
    }

    // Filter Type 2: Low Shelf
    void setLowShelf(float centerFreq, float gaindB, float Q = 0.707) {
        float sampleRate = 44100.0;
        float w0 = 2 * PI * centerFreq / sampleRate;
        float A = pow(10, gaindB / 40.0);
        float alpha = sin(w0) / 2.0 * sqrt( (A + 1/A)*(1/Q - 1) + 2 );
        float cosw0 = cos(w0);
        float a0 = (A+1) + (A-1)*cosw0 + 2*sqrt(A)*alpha;
        b0 = (A*((A+1) - (A-1)*cosw0 + 2*sqrt(A)*alpha)) / a0;
        b1 = (2*A*((A-1) - (A+1)*cosw0)) / a0;
        b2 = (A*((A+1) - (A-1)*cosw0 - 2*sqrt(A)*alpha)) / a0;
        a1 = (-2*((A-1) + (A+1)*cosw0)) / a0;
        a2 = ((A+1) + (A-1)*cosw0 - 2*sqrt(A)*alpha) / a0;
    }

    // Filter Type 3: High Shelf
    void setHighShelf(float centerFreq, float gaindB, float Q = 0.707) {
        float sampleRate = 44100.0;
        float w0 = 2 * PI * centerFreq / sampleRate;
        float A = pow(10, gaindB / 40.0);
        float alpha = sin(w0) / 2.0 * sqrt( (A + 1/A)*(1/Q - 1) + 2 );
        float cosw0 = cos(w0);
        float a0 = (A+1) - (A-1)*cosw0 + 2*sqrt(A)*alpha;
        b0 = (A*((A+1) + (A-1)*cosw0 + 2*sqrt(A)*alpha)) / a0;
        b1 = (-2*A*((A-1) + (A+1)*cosw0)) / a0;
        b2 = (A*((A+1) + (A-1)*cosw0 - 2*sqrt(A)*alpha)) / a0;
        a1 = (2*((A-1) - (A+1)*cosw0)) / a0;
        a2 = ((A+1) - (A-1)*cosw0 - 2*sqrt(A)*alpha) / a0;
    }

    // Filter Type 4: High Pass
    void setHighPass(float cutoffFreq, float Q = 0.707) {
        float sampleRate = 44100.0;
        float w0 = 2 * PI * cutoffFreq / sampleRate;
        float alpha = sin(w0) / (2 * Q);
        float cosw0 = cos(w0);
        float a0 = 1 + alpha;
        b0 = (1 + cosw0) / 2 / a0; b1 = -(1 + cosw0) / a0; b2 = (1 + cosw0) / 2 / a0;
        a1 = (-2 * cosw0) / a0; a2 = (1 - alpha) / a0;
    }

    // Process Stereo Sample
    inline void process(float &l, float &r) {
        float out_l = b0*l + b1*x1_l + b2*x2_l - a1*y1_l - a2*y2_l;
        x2_l = x1_l; x1_l = l; y2_l = y1_l; y1_l = out_l;
        l = out_l;
        float out_r = b0*r + b1*x1_r + b2*x2_r - a1*y1_r - a2*y2_r;
        x2_r = x1_r; x1_r = r; y2_r = y1_r; y1_r = out_r;
        r = out_r;
    }
};

// --- INCLUDE VINTAGE SUITE ---
// This must be INCLUDED AFTER Biquad is defined
#include "vintage.h"

// ==========================================================
// MAIN DSP ENGINE
// ==========================================================
class AudioDSP {
public:
    volatile bool isUpdating = false;

    // --- STATES ---
    bool eqEnabled = true;
    bool stereoExpand = false;
    bool subsonicFilter = false;
    bool loudnessEnabled = false;
    float outputGain = 1.0;

    // PREAMP MODE (AUX Input)
    // 0 = Flat
    // 1 = RIAA
    // 2 = Dolby B
    // 3 = Dolby C
    // 4 = DBX
    int preampMode = 0;

    // State Tracking
    float eqGains[10];

    // --- ENGINES ---
    std::vector<Biquad> eqFilters;
    Biquad subsonicFilterBP;
    LoudnessEngine loudL, loudR;
    StereoExpander expander;

    // --- VINTAGE ENGINES (From vintage.h) ---
    RIAA_Engine riaa;
    DolbyB_Engine dolbyB;
    DolbyC_Engine dolbyC;
    DBX_Engine dbx;

    AudioDSP() {
        // 1. Init EQ (10 Bands)
        float freqs[] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        for(int i=0; i<10; i++) {
            Biquad bq;
            bq.setPeaking(freqs[i], 0, 1.0); // Q=1.0 Musical
            eqFilters.push_back(bq);
            eqGains[i] = 0.0;
        }

        // 2. Init Subsonic (20Hz High Pass)
        subsonicFilterBP.setHighPass(20.0, 0.707);

        // 3. Init Effects
        Loudness_Init(&loudL);
        Loudness_Init(&loudR);
        StereoExpander_Init(&expander);
        StereoExpander_SetWidth(&expander, 1.5f);

        // 4. Init Vintage Engines
        riaa.init();
        dolbyB.init();
        dolbyC.init();
        dbx.init();
    }

    void updateEQBand(int index, float gaindB) {
        float freqs[] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        if(index >= 0 && index < 10) {
            eqFilters[index].setPeaking(freqs[index], gaindB, 1.0);
            eqGains[index] = gaindB;
            eqFilters[index].resetState(); // Prevent POP
        }
    }

    void setVolume(int step) {
        int dspStep = (step * 100) / 30;
        if (dspStep > 100) dspStep = 100;
        Loudness_SetVolumeStep(&loudL, dspStep);
        Loudness_SetVolumeStep(&loudR, dspStep);
    }

    // =========================================================
    // PART 1: PREAMP STAGE (AUX INPUT)
    // =========================================================
    StereoSample processAuxPreamp(StereoSample input) {
        float l = (float)input.l;
        float r = (float)input.r;

        switch(preampMode) {
            case 1: riaa.process(l, r); break;   // RIAA
            case 2: dolbyB.process(l, r); break; // Dolby B
            case 3: dolbyC.process(l, r); break; // Dolby C
            case 4: dbx.process(l, r); break;    // DBX
            default: break; // 0 = Line (Flat)
        }

        return { (int32_t)l, (int32_t)r };
    }

    // =========================================================
    // PART 2: MASTER CHAIN (ALL SOURCES)
    // =========================================================
    inline StereoSample processMasterChain(StereoSample input) {
        if (isUpdating) return {0, 0};

        float l = (float)input.l;
        float r = (float)input.r;

        // 1. Gain
        l *= outputGain;
        r *= outputGain;

        // 2. Subsonic
        if (subsonicFilter) subsonicFilterBP.process(l, r);

        // 3. EQ (With Optimization)
        if (eqEnabled) {
            for(int i=0; i<10; i++) {
                // CPU OPTIMIZATION: Skip bands with 0 gain
                if(fabs(eqGains[i]) > 0.1f) {
                    eqFilters[i].process(l, r);
                }
            }
        }

        // 4. Stereo Expander
        static bool lastExpState = false;
        if (stereoExpand != lastExpState) {
             StereoExpander_SetState(&expander, stereoExpand ? 1 : 0);
             lastExpState = stereoExpand;
        }
        if (stereoExpand) StereoExpander_Process(&expander, &l, &r);

        // 5. Loudness
        static bool lastLoudState = false;
        if (loudnessEnabled != lastLoudState) {
            Loudness_SetState(&loudL, loudnessEnabled ? 1 : 0);
            Loudness_SetState(&loudR, loudnessEnabled ? 1 : 0);
            lastLoudState = loudnessEnabled;
        }
        if (loudnessEnabled) {
            l = Loudness_ProcessSample(&loudL, l);
            r = Loudness_ProcessSample(&loudR, r);
        }

        // 6. Hard Limit
        if (l > 2147000000.0f) l = 2147000000.0f; else if (l < -2147000000.0f) l = -2147000000.0f;
        if (r > 2147000000.0f) r = 2147000000.0f; else if (r < -2147000000.0f) r = -2147000000.0f;

        return { (int32_t)l, (int32_t)r };
    }
};

#endif
