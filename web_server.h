#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "dsp_engine.h"

// --- Externs ---
extern WebServer server;
extern AudioDSP dsp;
extern Preferences preferences;
extern String btName, wifiSSID, wifiPass;

// Signal Generator Globals
extern bool genActive;
extern int genSignalType;
extern float genFreqStart;
extern float genFreqEnd;
extern float genPeriod;

// Include the HTML content
#include "html1.h"

// --- API HANDLERS ---

void handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

// 2. Handle DSP Parameter Updates with Safe Pausing
void handleDSPConfig() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        // --- STEP 1: PAUSE ENGINE ---
        // Tell the audio thread to output silence
        dsp.isUpdating = true;

        // --- STEP 2: WAIT FOR BUFFER FLUSH ---
        // Wait approx 150ms for existing audio in buffers to drain out
        delay(150);

        // --- STEP 3: APPLY SETTINGS ---
        dsp.stereoExpand = doc["stereo"];
        dsp.subsonicFilter = doc["subsonic"];
        dsp.eqEnabled = doc["eqEnable"]; 
        dsp.outputGain = (float)doc["gain"] / 100.0f;

        // Update EQ Bands
        JsonArray eq = doc["eq"];
        if (!eq.isNull()) {
            for(int i=0; i<10; i++) {
                dsp.updateEQBand(i, eq[i]);
            }
        }

        // --- STEP 4: STABILIZATION DELAY ---
        // Allow calculations to settle
        delay(50);

        // --- STEP 5: RESUME ENGINE ---
        dsp.isUpdating = false;

        server.send(200, "text/plain", "DSP Updated");
    } else {
        server.send(400, "text/plain", "No Data");
    }
}

void handleGenConfig() {
     if (server.hasArg("plain")) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, server.arg("plain"));

        genActive = doc["active"];
        genSignalType = doc["type"];
        genFreqStart = doc["fStart"];
        genFreqEnd = doc["fEnd"];
        genPeriod = doc["period"];

        server.send(200, "text/plain", "Gen Updated");
     }
}

// 4. Handle Save Preset (Saves what the UI sends, ensuring consistency)
void handleSavePreset() {
    if (server.hasArg("plain")) {
        DynamicJsonDocument doc(2048); 
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        int id = doc["id"];
        String key = "p" + String(id);

        // We re-serialize the RELEVANT parts of the incoming JSON to storage.
        // This ensures "What You See (in UI) Is What You Save".
        DynamicJsonDocument store(2048);

        store["stereo"] = doc["stereo"];
        store["subsonic"] = doc["subsonic"];
        store["eqEnable"] = doc["eqEnable"];
        store["gain"] = doc["gain"]; 
        
        JsonArray eqVals = store.createNestedArray("eq");
        JsonArray incomingEq = doc["eq"];
        for(int i=0; i<10; i++) {
             eqVals.add(incomingEq[i]);
        }

        String output;
        serializeJson(store, output);
        preferences.putString(key.c_str(), output);
        
        // Also update the live DSP to match what we just saved (Safety sync)
        dsp.stereoExpand = doc["stereo"];
        dsp.subsonicFilter = doc["subsonic"];
        dsp.eqEnabled = doc["eqEnable"];
        // Note: We don't trigger the full Pause/Resume here to avoid double-hiccups,
        // assuming the user hit "Apply" before "Save". 
        // If they didn't, the next "Apply" will sync it.

        server.send(200, "text/plain", "Preset Saved");
    }
}

void handleLoadPreset() {
    if (server.hasArg("id")) {
        String id = server.arg("id");
        String key = "p" + id;

        if(preferences.isKey(key.c_str())) {
            String json = preferences.getString(key.c_str());
            server.send(200, "application/json", json);
        } else {
            server.send(404, "text/plain", "Preset Empty");
        }
    }
}

void handleSystemConfig() {
    if (server.hasArg("plain")) {
         DynamicJsonDocument doc(512);
         deserializeJson(doc, server.arg("plain"));
         String type = doc["type"];

         if(type == "bt") {
             String name = doc["name"];
             preferences.putString("bt_name", name);
         } else if (type == "wifi") {
             String ssid = doc["ssid"];
             String pass = doc["pass"];
             preferences.putString("wifi_ssid", ssid);
             preferences.putString("wifi_pass", pass);
         }
         server.send(200, "text/plain", "Config Saved. Reboot to Apply.");
    }
}

void initWebServer() {
    server.on("/", handleRoot);
    server.on("/api/dsp", HTTP_POST, handleDSPConfig);
    server.on("/api/gen", HTTP_POST, handleGenConfig);
    server.on("/api/savePreset", HTTP_POST, handleSavePreset);
    server.on("/api/preset", HTTP_GET, handleLoadPreset);
    server.on("/api/config", HTTP_POST, handleSystemConfig);

    server.begin();
}

#endif