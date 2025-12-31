#ifndef AUDIOCB_H
#define AUDIOCB_H

// --- EXTERNAL REFERENCES TO MAIN ---
extern int volume;
extern int genSignalType;
extern float genFreqStart;
extern float genFreqEnd;
extern float genPeriod;
extern unsigned long sweepStartTime;
extern double currentPhase;

// Bluetooth Callback
void bt_data_callback(const uint8_t *data, uint32_t length) {
    int16_t* editable_samples = (int16_t*)data;
    uint32_t sample_count = length / 2;

    for(int i=0; i < sample_count; i+=2) {
         StereoSample s = { (int32_t)editable_samples[i] << 16, (int32_t)editable_samples[i+1] << 16 };
         
         // 1. Process Master Chain (EQ, Expander, Loudness Filter)
         s = dsp.processMasterChain(s);
         
         // 2. Apply Volume Attenuation
         float volFactor = (float)volume / 30.0f;
         s.l *= volFactor; s.r *= volFactor;

         editable_samples[i]   = (int16_t)(s.l >> 16);
         editable_samples[i+1] = (int16_t)(s.r >> 16);
    }
    
    // Safety: Do not block forever. 10 ticks timeout prevents BT crash.
    size_t bytes_written;
    i2s_write(I2S_NUM_0, data, length, &bytes_written, 10); 
}

// AUX Loop 
void handleAuxLoop() {
    size_t bytes_read, bytes_written;
    int32_t buffer[128]; 

    // Read from ADC
    // [FIX] Changed portMAX_DELAY to 10ms timeout.
    // This allows the main loop to continue scanning buttons even if the ADC stream stops.
    i2s_read(I2S_NUM_1, buffer, sizeof(buffer), &bytes_read, 10 / portTICK_PERIOD_MS);
    
    if (bytes_read > 0) {
        int samples = bytes_read / 4;
        for (int i=0; i<samples; i+=2) {
             StereoSample s = { buffer[i], buffer[i+1] };
             
             // 1. Preamp (RIAA / Dolby)
             s = dsp.processAuxPreamp(s);
             
             // 2. Master Chain (EQ, Expander, Loudness)
             s = dsp.processMasterChain(s);

             // 3. Volume Attenuation
             float volFactor = (float)volume / 30.0f;
             s.l *= volFactor; s.r *= volFactor;

             buffer[i] = s.l;
             buffer[i+1] = s.r;
        }
        i2s_write(I2S_NUM_0, buffer, bytes_read, &bytes_written, portMAX_DELAY);
    }
}

// Gen Loop
void handleGenLoop() {
    StereoSample s;
    float sampleL = 0;
    
    // [SAFETY] Prevent Division by Zero if period is invalid
    int periodMs = (int)(genPeriod * 1000);
    if (periodMs < 1) periodMs = 1; 

    if (genSignalType == 0) { 
        // Sine Wave
        sampleL = sin(currentPhase);
        currentPhase += 2.0 * PI * genFreqStart / 44100.0;
        if (currentPhase > 2.0 * PI) currentPhase -= 2.0 * PI;
    }
    else if (genSignalType == 1) { 
        // White Noise - [FIX] Replaced rand() with fastWhiteNoise()
        sampleL = fastWhiteNoise();
    }
    else if (genSignalType == 2) { 
        // Pink Noise
        sampleL = generatePinkNoise();
    }
    else if (genSignalType == 3) { 
        // Sweep
        unsigned long now = millis();
        // Use safe periodMs calculated above
        float t = (float)((now - sweepStartTime) % periodMs) / 1000.0;
        float currentFreq = genFreqStart + (genFreqEnd - genFreqStart) * (t / genPeriod);
        sampleL = sin(currentPhase);
        currentPhase += 2.0 * PI * currentFreq / 44100.0;
        if (currentPhase > 2.0 * PI) currentPhase -= 2.0 * PI;
    }

    // [FIX] Changed 0.5 to 1.0f. Allows full scale (0dBFS) output when volume is max.
    s.l = (int32_t)(sampleL * 2147483647.0f * 1.0f); 
    s.r = s.l; 
    
    // Signal Gen bypasses Preamp, goes strictly to Master
    s = dsp.processMasterChain(s);
    
    float volFactor = (float)volume / 30.0f;
    s.l *= volFactor; s.r *= volFactor;

    size_t w;
    i2s_write(I2S_NUM_0, &s, sizeof(s), &w, portMAX_DELAY);
}
#endif