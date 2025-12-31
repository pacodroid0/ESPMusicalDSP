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
    // data_cb: Funzione che riceve l'audio (bt_data_callback in audiocb.h)
    // meta_cb: Funzione opzionale per leggere Titolo/Artista (per il Display)
    // [FIXED] Updated signature: id is now uint8_t (not pointer), and fixed parens syntax
    void startRX(void (*data_cb)(const uint8_t*, uint32_t), void (*meta_cb)(uint8_t, const uint8_t*) = nullptr) {
        // Se eravamo in TX, spegni tutto
        if (isTxMode) stop();
        isTxMode = false;

        // Configura Callback Audio (stream reader)
        sink.set_stream_reader(data_cb);

        // Configura Callback Metadata (opzionale per Display)
        if (meta_cb != nullptr) {
            sink.set_avrc_metadata_callback(meta_cb);
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
    // targetName: (Opzionale) Nome cuffie a cui connettersi. Se vuoto, cerca l'ultimo o il più forte.
    // [FIXED] Updated signature: data is now Frame* (not uint8_t*)
    void startTX(int32_t (*provider_cb)(Frame*, int32_t), String targetName = "") {
        // Se eravamo in RX, spegni tutto
        if (!isTxMode) stop();
        isTxMode = true;

        // Configurazione Source
        source.set_auto_reconnect(true);

        // Se volessimo filtrare per nome (richiede logica callback ssid extra),
        // per ora usiamo la connessione standard automatica.

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

    // Volume pass-through (se gestito dalla lib BT e non dal DSP)
    void setVolume(uint8_t vol) {
        if (!isTxMode) sink.set_volume(vol);
        else source.set_volume(vol);
    }
};

#endif