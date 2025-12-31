#ifndef BLUESTREAM_H
#define BLUESTREAM_H

#include <Arduino.h>
#include "BluetoothA2DPSink.h"
#include "BluetoothA2DPSource.h"

// Wrapper class to handle both Receiver (Sink) and Transmitter (Source)
class BlueStream {
private:
    BluetoothA2DPSink sink;
    BluetoothA2DPSource source;

    bool isTxMode = false; // false = Sink (RX), true = Source (TX)
    String deviceName;     // My Name (for RX)

public:
    BlueStream() {}

    void init(String name) {
        deviceName = name;
    }

    // ==========================================
    // RX MODE (SINK) - Smartphone -> ESP32
    // ==========================================
    // data_cb: Funzione che riceve l'audio (bt_data_callback)
    // meta_cb: Funzione opzionale per leggere Titolo/Artista
    // vol_cb:  [NEW] Funzione opzionale per sincronizzare il volume (Telefono -> ESP32)
    void startRX(void (*data_cb)(const uint8_t*, uint32_t), 
                 void (*meta_cb)(uint8_t, const uint8_t*) = nullptr,
                 void (*vol_cb)(int) = nullptr) {
        
        // Se eravamo in TX, spegni tutto
        if (isTxMode) stop();
        isTxMode = false;

        // Configura Callback Audio (stream reader)
        sink.set_stream_reader(data_cb);

        // Configura Callback Metadata (opzionale per Display)
        if (meta_cb != nullptr) {
            sink.set_avrc_metadata_callback(meta_cb);
        }

        // [NEW] Configura Callback Volume (opzionale per sincronizzazione)
        if (vol_cb != nullptr) {
            sink.set_avrc_rn_volumechange(vol_cb);
        }

        // Avvia
        sink.start(deviceName.c_str());

        // Tweaks per stabilità
        // sink.set_auto_reconnect(true); // Opzionale
    }

    void disconnectRX() {
        if (!isTxMode && sink.is_connected()) {
            sink.disconnect();
        }
    }

    // ==========================================
    // TX MODE (SOURCE) - ESP32 -> Headphones
    // ==========================================
    // provider_cb: Funzione che fornisce l'audio (legge ADC -> DSP -> BT)
    // targetName: (Opzionale) Nome cuffie a cui connettersi.
    void startTX(int32_t (*provider_cb)(Frame*, int32_t), String targetName = "") {
        // Se eravamo in RX, spegni tutto
        if (!isTxMode) stop();
        isTxMode = true;

        // Configurazione Source
        source.set_auto_reconnect(true);

        // Avvia con la callback che fornisce i dati
        source.start(provider_cb);
    }

    // ==========================================
    // COMMON CONTROLS
    // ==========================================
    void stop() {
        if (isTxMode) {
            source.end();
        } else {
            sink.end();
        }
        // [FIX FROM MAIN] Safety Delay to prevent Pop/Click
        delay(200);
    }

    bool isConnected() {
        if (isTxMode) return source.is_connected();
        return sink.is_connected();
    }

    bool isTransmitting() {
        return isTxMode;
    }

    // Volume pass-through
    void setVolume(uint8_t vol) {
        if (!isTxMode) sink.set_volume(vol);
        else source.set_volume(vol);
    }
};

#endif