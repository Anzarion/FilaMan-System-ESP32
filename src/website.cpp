#include "website.h"
#include "commonFS.h"
#include "api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "nfc.h"
#include "nfc_acepro.h"
#include <Adafruit_PN532.h>
#include "scale.h"
#include "esp_task_wdt.h"
#include <Update.h>
#include "display.h"
#include "ota.h"
#include "config.h"
#include "debug.h"
#include "lang.h"

#ifndef VERSION
  #define VERSION "1.2.0"
#endif

#define NO_CACHE "no-cache, no-store, must-revalidate"
#define CACHE_ASSETS "max-age=86400"  // 24h Cache für statische Assets

AsyncWebServer server(webserverPort);
AsyncWebSocket ws("/ws");

uint8_t lastSuccess = 0;
nfcReaderStateType lastnfcReaderState = NFC_IDLE;

// Template-Processor für dynamische Seiten (ESPAsyncWebServer built-in)
// Ersetzt %variable% Platzhalter in HTML-Dateien
String templateProcessor(const String& var) {
    if (var == "registered") return filamanRegistered ? "Registered" : "Not Registered";
    if (var == "filamanUrl") return filamanUrl;
    if (var == "autoTare") return autoTare ? "checked" : "";
    return String();  // Unbekannte Variable - leer zurückgeben
}

void sendNfcDataToClient(AsyncWebSocketClient *client) {
    if(!client) return;
    switch(nfcReaderState){
        case NFC_IDLE: client->text("{\"type\":\"nfcData\", \"payload\":{}}"); break;
        case NFC_READ_SUCCESS: client->text("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}"); break;
        case NFC_READ_ERROR: client->text("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Read Error\"}}"); break;
        case NFC_WRITING: client->text("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Writing...\"}}"); break;
        case NFC_WRITE_SUCCESS: client->text("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Success\"}}"); break;
        case NFC_WRITE_ERROR: client->text("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Write Error\"}}"); break;
        default: break;
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        sendNfcDataToClient(client);
        client->text("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(lastSuccess) + "}}");
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("WS Client #%u error: %u\n", client->id(), *((uint16_t*)arg));
    } else if (type == WS_EVT_DATA) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char*)data, len);
        if (error) return;

        if (doc["type"] == "heartbeat") {
            ws.text(client->id(), "{"
                "\"type\":\"heartbeat\","
                "\"freeHeap\":" + String(ESP.getFreeHeap()/1024) + ","
                "\"filaman_connected\":" + String(filamanConnected) + ","
                "\"registered\":" + String(filamanRegistered) + ","
                "\"autoTare\":" + String(autoTare ? "true" : "false") + ""
                "}");
        }
        else if (doc["type"] == "writeNfcTag") {
            if (doc["payload"].is<JsonObject>()) {
                String payloadString;
                serializeJson(doc["payload"], payloadString);
                startWriteJsonToTag((doc["tagType"] == "spool") ? true : false, payloadString.c_str());
            }
        }

        else if (doc["type"] == "writeNfcTagBinary") {
            // ACE Pro binary format write (hybrid: ACE Pro + NDEF)
            if (doc["spoolId"].is<int>() && doc["payload"].is<JsonObject>()) {
                String spoolId = String(doc["spoolId"].as<int>());
                String payloadString;
                serializeJson(doc["payload"], payloadString);
                Serial.printf("[WebSocket] Initiating ACE Pro binary write for spool ID: %s\n", spoolId.c_str());
                startWriteNfcTagBinary(spoolId.c_str(), payloadString.c_str());
            } else {
                Serial.println("[WebSocket] Invalid spoolId/payload for writeNfcTagBinary");
            }
        }
        else if (doc["type"] == "scale") {
            if (doc["payload"] == "tare") {
                scaleTareRequest = true;
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
            else if (doc["payload"] == "calibrate") {
                scaleCalibrationRequest = true;
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
            else if (doc["payload"] == "setAutoTare") {
                setAutoTare(doc["enabled"].as<bool>());
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
        }
        else if (doc["type"] == "reconnect") {
            if (doc["payload"] == "filaman") {
                sendHeartbeatAsync();
            }
        }
    }
}

void sendWriteResult(AsyncWebSocketClient *client, uint8_t success) {
    String response = "{\"type\":\"writeNfcTag\",\"success\":" + String(success ? "1" : "0") + "}";
    if (client) client->text(response); else ws.textAll(response);
}

void foundNfcTag(AsyncWebSocketClient *client, uint8_t success) {
    if (success == lastSuccess && client == nullptr) return;
    ws.textAll("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(success) + "}}");
    lastSuccess = success;
}

void sendNfcData() {
    switch(nfcReaderState){
        case NFC_IDLE: ws.textAll("{\"type\":\"nfcData\", \"payload\":{}}"); break;
        case NFC_READ_SUCCESS: ws.textAll("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}"); break;
        case NFC_READ_ERROR: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Read Error\"}}"); break;
        case NFC_WRITING: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Writing...\"}}"); break;
        case NFC_WRITE_SUCCESS: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Success\"}}"); break;
        case NFC_WRITE_ERROR: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Write Error\"}}"); break;
        default: break;
    }
}

void setupWebserver(AsyncWebServer &server) {
    oledShowProgressBar(2, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_WEBSERVER_INIT));

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ===== STATISCHE ASSETS mit serveStatic() =====
    // Optimiert für concurrent Requests, mit 24h Browser-Cache
    // serveStatic() ist nicht-blockierend und handhabt parallele Requests besser
    server.serveStatic("/style.css", LittleFS, "/style.css")
        .setCacheControl(CACHE_ASSETS);
    server.serveStatic("/logo.png", LittleFS, "/logo.png")
        .setCacheControl(CACHE_ASSETS);
    server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico")
        .setCacheControl(CACHE_ASSETS);

    // ===== STATISCHE HTML-SEITEN (ohne Template-Ersetzung) =====
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /");
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /wifi");
        request->send(LittleFS, "/wifi.html", "text/html");
    });

    server.on("/upgrade", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Web: Request /upgrade");
        request->send(LittleFS, "/upgrade.html", "text/html");
    });

    server.on("/version.txt", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/version.txt", "text/plain");
    });

    // ===== DYNAMISCHE HTML-SEITEN (mit Template-Processor) =====
    // Nutzt den eingebauten Template-Processor für %variable% Ersetzung
    // Nicht-blockierend: Datei wird gestreamt und Variablen on-the-fly ersetzt
    server.on("/waage", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /waage");
        request->send(LittleFS, "/waage.html", "text/html", false, templateProcessor);
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /setup");
        request->send(LittleFS, "/setup.html", "text/html", false, templateProcessor);
    });

    // API Routes
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["url"] = filamanUrl;
        doc["registered"] = filamanRegistered;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/register", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (doc["url"].is<String>()) filamanUrl = doc["url"].as<String>();
        saveFilamanConfig();
        if (registerDevice(doc["code"].as<String>())) {
            request->send(200, "application/json", "{\"success\": true}");
        } else {
            request->send(400, "application/json", "{\"success\": false}");
        }
    });

    server.on("/api/v1/rfid/write", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
            return;
        }

        // Check if NFC is busy
        if (nfcWriteInProgress) {
            request->send(503, "application/json", "{\"error\": \"NFC busy\"}");
            return;
        }

        String payloadString;
        serializeJson(doc, payloadString);

        bool hasSpoolId = !doc["spool_id"].isNull() || !doc["sm_id"].isNull();
        int spoolId = doc["spool_id"] | 0;
        if (spoolId == 0 && doc["sm_id"].is<String>()) {
            spoolId = doc["sm_id"].as<String>().toInt();
        }
        int locationId = doc["location_id"] | 0;

        bool wantAcePro = aceProHybridMode || (doc["format"].is<String>() && doc["format"].as<String>() == "acepro");
        if (hasSpoolId && spoolId > 0 && wantAcePro) {
            // Hybrid write: ACE Pro binary (pages 4-34) + OpenSpool NDEF (pages 40+)
            Serial.printf("[RFID-Write] Hybrid ACE Pro write for spool %d\n", spoolId);
            startWriteNfcTagBinary(String(spoolId).c_str(), payloadString.c_str());
        } else {
            startWriteJsonToTag(hasSpoolId, payloadString.c_str(), spoolId, locationId);
        }

        // Respond immediately
        request->send(200, "application/json", "{\"success\": true, \"message\": \"Schreibvorgang wurde gestartet. Bitte Tag bereit halten...\"}");
    });

    server.on("/api/dump", HTTP_GET, [](AsyncWebServerRequest *request){
        // DEBUG: raw tag dump, pages 0-45
        extern Adafruit_PN532 nfc;
        if (nfcWriteInProgress) { request->send(503, "text/plain", "busy"); return; }
        vTaskSuspend(RfidReaderTask);
        String out = "";
        uint8_t uid[7]; uint8_t uidLength;
        if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500)) {
            char buf[24];
            for (uint8_t page = 0; page < 46; page++) {
                uint8_t data[4];
                if (nfc.ntag2xx_ReadPage(page, data)) {
                    snprintf(buf, sizeof(buf), "%02d: %02X %02X %02X %02X\n", page, data[0], data[1], data[2], data[3]);
                    out += buf;
                } else { out += String(page) + ": READ FAIL\n"; }
            }
        } else { out = "NO TAG\n"; }
        vTaskResume(RfidReaderTask);
        request->send(200, "text/plain", out);
    });
    server.on("/api/acepro", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("enabled")) {
            setAceProMode(request->getParam("enabled")->value() == "1");
        }
        request->send(200, "application/json", String("{\"hybrid\": ") + (aceProHybridMode ? "true" : "false") + "}");
    });

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"version\": \"" VERSION "\"}");
    });

    // Language API
    server.on("/api/language", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["lang"] = getLangCode();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/language", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        String lang = doc["lang"] | "";
        if (lang == "de") {
            saveLanguage(LANG_DE);
        } else if (lang == "en") {
            saveLanguage(LANG_EN);
        } else {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid language\"}");
            return;
        }
        request->send(200, "application/json", "{\"success\": true}");
    });

    // Display Settings API
    server.on("/api/display", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["sleepTimeout"] = oledSleepTimeout;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/display", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["sleepTimeout"].is<int>()) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing sleepTimeout\"}");
            return;
        }
        uint16_t timeout = (uint16_t)constrain((int)doc["sleepTimeout"], 0, 3600);
        saveOledSleepTimeout(timeout);
        request->send(200, "application/json", "{\"success\": true}");
    });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });

    handleUpdate(server);

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("Webserver gestartet");
}
