#ifndef VINTAGE_H
#define VINTAGE_H

#include <math.h>
#include <Arduino.h>

// ==========================================================
// SHARED HELPER: ENVELOPE FOLLOWER
// ==========================================================
class EnvelopeFollower {
private:
    float envelope = 0.0f;
    float attackCoef = 0.1f;
    float releaseCoef = 0.005f;
public:
    // Configures response speed.
    // fastAttack (ms): How quickly it reacts to loud peaks.
    // slowRelease (ms): How slowly it fades out (prevents "pumping").
    void init(float attackMs, float releaseMs, float sampleRate = 44100.0f) {
        // Calculate coefficients for 1-pole LPF
        attackCoef = 1.0f - expf(-1.0f / (attackMs * 0.001f * sampleRate));
        releaseCoef = 1.0f - expf(-1.0f / (releaseMs * 0.001f * sampleRate));
        envelope = 0.0f;
    }

    inline float process(float in) {
        float absIn = fabsf(in);
        if (absIn > envelope) envelope += attackCoef * (absIn - envelope);
        else envelope += releaseCoef * (absIn - envelope);
        return envelope;
    }
};

// ==========================================================
// 1. RIAA ENGINE (Vinyl Phono Stage)
// ==========================================================
class RIAA_Engine {
private:
    Biquad lowShelf, highShelf;
public:
    void init() {
        // Standard RIAA Curve: Bass Boost (+20dB), Treble Cut (-20dB)
        lowShelf.setLowShelf(500.0, 19.0, 0.707);
        highShelf.setHighShelf(2122.0, -19.0, 0.707);
    }

    inline void process(float &l, float &r) {
        lowShelf.process(l, r);
        highShelf.process(l, r);
    }
};

// ==========================================================
// 2. DOLBY B ENGINE (Single Stage Tape NR)
// ==========================================================
class DolbyB_Engine {
private:
    Biquad filter;
    EnvelopeFollower env;
    int skipCounter = 0;

public:
    void init() {
        filter.setHighShelf(5000.0, 0.0, 1.0); // Start Flat
        env.init(10.0f, 100.0f); // Fast attack (10ms), Medium release (100ms)
    }

    inline void process(float &l, float &r) {
        // Mono detection for stereo link
        float lvl = env.process((l + r) * 0.5f);

        // OPTIMIZATION: Update coefficients only every 64 samples
        if (++skipCounter >= 64) {
            float threshold = 0.25f; // ~ -12dB activation point
            float gainCut = 0.0f;

            if (lvl < threshold) {
                // Linear map: 0..Threshold -> -10dB..0dB
                float ratio = (threshold - lvl) / threshold;
                gainCut = -10.0f * ratio;
                if (gainCut < -10.0f) gainCut = -10.0f;
            }
            filter.setHighShelf(5000.0, gainCut, 0.707);
            skipCounter = 0;
        }

        filter.process(l, r);
    }
};

// ==========================================================
// 3. DOLBY C ENGINE (Dual Stage Tape NR)
// ==========================================================
// Implements two sliding bands (High and Mid) for ~20dB reduction
class DolbyC_Engine {
private:
    Biquad highFilter, midFilter;
    EnvelopeFollower env;
    int skipCounter = 0;

public:
    void init() {
        highFilter.setHighShelf(6000.0, 0.0, 1.0); // High Band
        midFilter.setHighShelf(1000.0, 0.0, 1.0);  // Mid Band (Overlap)
        env.init(5.0f, 80.0f); // Slightly faster than B
    }

    inline void process(float &l, float &r) {
        float lvl = env.process((l + r) * 0.5f);

        if (++skipCounter >= 64) {
            float threshold = 0.35f; // Activates earlier (~ -9dB)
            float hGain = 0.0f;
            float mGain = 0.0f;

            if (lvl < threshold) {
                float ratio = (threshold - lvl) / threshold;
                if(ratio > 1.0f) ratio = 1.0f;

                // Dolby C aggressive cut:
                hGain = -12.0f * ratio; // Highs get cut up to 12dB
                mGain = -10.0f * ratio; // Mids get cut up to 10dB
                // Total perceived reduction ~22dB
            }

            highFilter.setHighShelf(6000.0, hGain, 0.707);
            midFilter.setHighShelf(1000.0, mGain, 0.707);
            skipCounter = 0;
        }

        // Cascade Processing: Mid -> High
        midFilter.process(l, r);
        highFilter.process(l, r);
    }
};

// ==========================================================
// 4. DBX TYPE II ENGINE (Broadband Expander)
// ==========================================================
// 1:2 Linear Expansion.
// Logic: If input is below Pivot (-6dB), attenuate it further.
// This pushes the noise floor down drastically.
class DBX_Engine {
private:
    EnvelopeFollower env;

public:
    void init() {
        // DBX timings are critical to avoid "pumping" artifacts.
        // Type II uses RMS, we approximate with average.
        // Attack 10ms, Release 50ms (Logarithmic feel)
        env.init(10.0f, 50.0f);
    }

    inline void process(float &l, float &r) {
        // 1. Detect Signal Level
        float signal = (fabsf(l) + fabsf(r)) * 0.5f;
        float lvl = env.process(signal);

        // 2. Define Pivot Point (The "0VU" level)
        // 0.5 (approx -6dBFS) is a safe "Line Level" assumption for 16/32bit audio.
        // Signals above 0.5 get Boosted (Compressed on tape, Expanded here)
        // Signals below 0.5 get Attenuated (Expanded downward)
        float pivot = 0.5f;

        float gain = 1.0f;

        // 3. Calculate Expansion Gain
        // For 1:2 Expansion Ratio: Gain = Envelope / Pivot
        if (lvl > 0.001f) {
            gain = lvl / pivot;

            // Clamp gain to prevent infinite silence or explosion
            if (gain < 0.1f) gain = 0.1f; // Max attenuation -20dB (Noise Floor)
            if (gain > 2.0f) gain = 2.0f; // Max boost +6dB
        } else {
            gain = 0.0f; // Silence
        }

        // 4. Apply
        l *= gain;
        r *= gain;
    }
};

#endif
