#ifndef PNOISE_H
#define PNOISE_H

// Optimized Pink Noise Generator
// Uses an LFSR for White Noise (Faster than rand())
static uint32_t lfsr_state = 1;

static inline float fastWhiteNoise() {
    // 32-bit Xorshift
    uint32_t x = lfsr_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    lfsr_state = x;
    return ((float)x / 4294967296.0f) * 2.0f - 1.0f;
}

static float pink_b0=0, pink_b1=0, pink_b2=0, pink_b3=0, pink_b4=0, pink_b5=0, pink_b6=0;

float generatePinkNoise() {
    float white = fastWhiteNoise();
    pink_b0 = 0.99886 * pink_b0 + white * 0.0555179;
    pink_b1 = 0.99332 * pink_b1 + white * 0.0750759;
    pink_b2 = 0.96900 * pink_b2 + white * 0.1538520;
    pink_b3 = 0.86650 * pink_b3 + white * 0.3104856;
    pink_b4 = 0.55000 * pink_b4 + white * 0.5329522;
    pink_b5 = -0.7616 * pink_b5 - white * 0.0168980;
    float pink = pink_b0 + pink_b1 + pink_b2 + pink_b3 + pink_b4 + pink_b5 + pink_b6 + white * 0.5362;
    pink_b6 = white * 0.115926;
    return pink * 0.11;
}

#endif