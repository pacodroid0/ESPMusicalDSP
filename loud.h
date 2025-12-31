/*
 * Loud.h - Vintage Hi-Fi Loudness DSP Engine
 * Implements Fletcher-Munson style compensation with Logarithmic Taper.
 *
 * Integration:
 * 1. #include "Loud.h"
 * 2. Create instance: LoudnessEngine myLoudness;
 * 3. Init: Loudness_Init(&myLoudness);
 * 4. On Volume Change: Loudness_SetVolumeStep(&myLoudness, currentStep);
 * 5. In Audio Loop: output = Loudness_ProcessSample(&myLoudness, input);
 */

#ifndef LOUD_H
#define LOUD_H

#include <math.h>
#include <stdint.h>

// ==========================================
// CONFIGURATION (70s/80s Hi-Fi Specs)
// ==========================================
#define LOUD_SAMPLE_RATE       44100.0f

// Corner Frequencies (Classic Pivot Points)
#define LOUD_BASS_FREQ         100.0f
#define LOUD_TREBLE_FREQ       10000.0f

// Filter Slope (0.707 = Butterworth = Smooth)
#define LOUD_Q                 0.707f

// Max Boost amount at Volume Step 0 (Silence)
#define MAX_BASS_BOOST_DB      12.0f
#define MAX_TREBLE_BOOST_DB    6.0f

// The Volume Step where Loudness disengages (Flat response)
// Steps 0 to 19 = Boost applied. Steps 20 to 30 = Flat.
#define LOUD_THRESHOLD_STEP    20

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ==========================================
// DATA STRUCTURES
// ==========================================

typedef struct {
    float b0, b1, b2, a1, a2; // Filter Coefficients
    float z1, z2;             // State Memory
} L_Biquad;

typedef struct {
    L_Biquad bassFilter;
    L_Biquad trebleFilter;
    int currentVolumeStep;
    int isEnabled;            // 1 = ON, 0 = OFF
} LoudnessEngine;

// ==========================================
// INTERNAL MATH HELPERS (Static Inline)
// ==========================================

// Transposed Direct Form II Processing (Best for floating point audio)
static inline float L_Biquad_Process(L_Biquad* f, float input) {
    float output = f->b0 * input + f->z1;
    f->z1 = f->b1 * input - f->a1 * output + f->z2;
    f->z2 = f->b2 * input - f->a2 * output;
    return output;
}

static inline void L_Biquad_Reset(L_Biquad* f) {
    f->z1 = 0.0f;
    f->z2 = 0.0f;
    f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
    f->a1 = 0.0f; f->a2 = 0.0f;
}

// Calculate Shelf Coefficients
// type: 0 = Low Shelf, 1 = High Shelf
static inline void L_Calc_Shelf(L_Biquad* f, float freq, float gainDB, int type) {
    // If gain is effectively 0, set to pass-through to save precision
    if (fabsf(gainDB) < 0.1f) {
        f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
        f->a1 = 0.0f; f->a2 = 0.0f;
        return;
    }

    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * freq / LOUD_SAMPLE_RATE;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / (2.0f * LOUD_Q);

    float b0, b1, b2, a0, a1, a2;

    if (type == 0) { // Low Shelf
        b0 =    A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
        b2 =    A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha);
        a0 =        (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
        a2 =        (A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha;
    } else { // High Shelf
        b0 =    A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
        b2 =    A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha);
        a0 =        (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
        a2 =        (A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha;
    }

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

// ==========================================
// PUBLIC API
// ==========================================

// 1. Initialize the Engine
static inline void Loudness_Init(LoudnessEngine* eng) {
    L_Biquad_Reset(&eng->bassFilter);
    L_Biquad_Reset(&eng->trebleFilter);
    eng->currentVolumeStep = 30; // Default to max (no effect)
    eng->isEnabled = 0;          // Default to off

    // Init filters to flat
    L_Calc_Shelf(&eng->bassFilter, LOUD_BASS_FREQ, 0.0f, 0);
    L_Calc_Shelf(&eng->trebleFilter, LOUD_TREBLE_FREQ, 0.0f, 1);
}

// 2. Set Volume and Recalculate Curves (The "Logarithmic Engine")
static inline void Loudness_SetVolumeStep(LoudnessEngine* eng, int step) {
    if (step < 0) step = 0;
    if (step > 30) step = 30;
    eng->currentVolumeStep = step;

    float bassGain = 0.0f;
    float trebleGain = 0.0f;

    if (eng->isEnabled && step < LOUD_THRESHOLD_STEP) {
        /* LOGARITHMIC MAPPING
           We map the linear step (0-20) to a logarithmic intensity ratio.
           Equation: ratio = 1 - (log10(step + 1) / log10(threshold + 1))

           Result:
           Step 0  -> Ratio 1.0 (100% Boost)
           Step 5  -> Ratio ~0.4 (40% Boost) - Drops fast, like real hearing
           Step 19 -> Ratio ~0.01 (1% Boost)
           Step 20 -> Ratio 0.0 (0% Boost)
        */

        float num = log10f((float)step + 1.0f);
        float den = log10f((float)LOUD_THRESHOLD_STEP + 1.0f);
        float ratio = 1.0f - (num / den);

        // Safety clamp
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        bassGain = MAX_BASS_BOOST_DB * ratio;
        trebleGain = MAX_TREBLE_BOOST_DB * ratio;
    }

    // Apply coefficients
    L_Calc_Shelf(&eng->bassFilter, LOUD_BASS_FREQ, bassGain, 0);
    L_Calc_Shelf(&eng->trebleFilter, LOUD_TREBLE_FREQ, trebleGain, 1);
}

// 3. Toggle Loudness Switch (ON/OFF)
static inline void Loudness_SetState(LoudnessEngine* eng, int enabled) {
    eng->isEnabled = enabled;
    // Force recalculation immediately
    Loudness_SetVolumeStep(eng, eng->currentVolumeStep);
}

// 4. Process Audio (Call inside your sample loop)
static inline float Loudness_ProcessSample(LoudnessEngine* eng, float input) {
    float temp = L_Biquad_Process(&eng->bassFilter, input);
    return L_Biquad_Process(&eng->trebleFilter, temp);
}

#endif // LOUD_H
