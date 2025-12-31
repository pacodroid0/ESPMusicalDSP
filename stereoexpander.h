/*
 * stereoexpander.h - Mid-Side Stereo Widening Engine
 *
 * Logic:
 * 1. Converts Left/Right to Mid (Sum) and Side (Difference).
 * 2. Boosts the Side component to increase width.
 * 3. Converts back to Left/Right.
 *
 * Integration:
 * 1. #include "StereoExpander.h"
 * 2. Init: StereoExpander_Init(&myExpander);
 * 3. Config: StereoExpander_SetWidth(&myExpander, 1.5f); // 1.5 = 150% width
 * 4. Process: StereoExpander_Process(&myExpander, &leftSample, &rightSample);
 */

#ifndef STEREOEXPANDER_H
#define STEREOEXPANDER_H

// ==========================================
// CONFIGURATION
// ==========================================

// Limits to prevent phase cancellation issues
#define MAX_WIDTH_FACTOR 2.0f  // 200% Width (Very wide)
#define MIN_WIDTH_FACTOR 0.0f  // 0% Width (Mono)

// ==========================================
// DATA STRUCTURES
// ==========================================

typedef struct {
    float currentWidth; // 1.0 = Normal, >1.0 = Wide
    int isEnabled;      // 1 = ON, 0 = OFF
} StereoExpander;

// ==========================================
// PUBLIC API
// ==========================================

// 1. Initialize
static inline void StereoExpander_Init(StereoExpander* exp) {
    exp->currentWidth = 1.0f; // Default to normal stereo (no effect)
    exp->isEnabled = 0;       // Default OFF
}

// 2. Set Width Amount
// width: 1.0 = Normal, 0.0 = Mono, 2.0 = Extra Wide
static inline void StereoExpander_SetWidth(StereoExpander* exp, float width) {
    if (width < MIN_WIDTH_FACTOR) width = MIN_WIDTH_FACTOR;
    if (width > MAX_WIDTH_FACTOR) width = MAX_WIDTH_FACTOR;
    exp->currentWidth = width;
}

// 3. Toggle ON/OFF
static inline void StereoExpander_SetState(StereoExpander* exp, int enabled) {
    exp->isEnabled = enabled;
}

// 4. Process Stereo Pair (In-Place Modification)
// Pass pointers to your Left and Right samples. The function modifies them directly.
static inline void StereoExpander_Process(StereoExpander* exp, float* left, float* right) {
    // Optimization: If disabled or set to 1.0 (normal), do nothing
    if (!exp->isEnabled || (exp->currentWidth > 0.99f && exp->currentWidth < 1.01f)) {
        return;
    }

    float l = *left;
    float r = *right;

    // --- STEP 1: ENCODE TO MID/SIDE ---
    // Mid  = (L + R) * 0.5
    // Side = (L - R) * 0.5
    float mid = (l + r) * 0.5f;
    float side = (l - r) * 0.5f;

    // --- STEP 2: APPLY WIDTH ---
    // We multiply the Side signal by our width factor.
    side *= exp->currentWidth;

    // --- STEP 3: DECODE BACK TO L/R ---
    // Left  = Mid + Side
    // Right = Mid - Side
    *left  = mid + side;
    *right = mid - side;
}

#endif // STEREO_EXPANDER_H
