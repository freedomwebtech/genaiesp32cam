#include <esp32cam.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "time.h"

// WiFi Credentials
const char* WIFI_SSID = "apex";
const char* WIFI_PASS = "freedomtech";

// Gemini AI API Key
const char* GEMINI_API_KEY = "AIzaSyCAjAb3klC4YV5-H7y09BNqriOG-QcVd8A";

// NTP Server for Date and Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;  // Adjust for your timezone
const int daylightOffset_sec = 0;

// Base64 Encoding Function
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t length) {
    String encoded = "";
    int i = 0, j = 0;
    uint8_t array_3[3], array_4[4];

    while (length--) {
        array_3[i++] = *(data++);
        if (i == 3) {
            array_4[0] = (array_3[0] & 0xfc) >> 2;
            array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
            array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
            array_4[3] = array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_table[array_4[i]];
            i = 0;
        }
    }
    return encoded;
}

// Get Current Date and Time
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time Error";
    }
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// Helmet Detection Function (Runs Every 5 Seconds)
void detectHelmet() {
    Serial.println("\n[+] Capturing Image...");
    auto frame = esp32cam::capture();
    if (frame == nullptr) {
        Serial.println("[-] Capture failed");
        return;
    }

    // Convert Image to Base64
    String base64Image = base64_encode(frame->data(), frame->size());

    // Send Image to Gemini AI
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + String(GEMINI_API_KEY);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"contents\":[{";
    payload += "\"parts\":[";
    payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}}";
    payload += ",{\"text\":\"Detect if a person is present and check if they are wearing a helmet. "
               "If no helmet is detected, mention the person. If multiple people are detected, analyze each individually.\"}";
    payload += "]}]}";

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        String response = http.getString();
        Serial.println("[+] Gemini AI Response: " + response);

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            return;
        }

        const char* aiText = doc["candidates"][0]["content"]["parts"][0]["text"];
        if (aiText) {
            String dateTime = getCurrentTime();
            Serial.println("\n======= AI Detection Result =======");
            Serial.println("📅 Date & Time: " + dateTime);
            Serial.println("📝 AI Response: " + String(aiText));
            Serial.println("====================================");
        } else {
            Serial.println("[-] No relevant answer found");
        }
    } else {
        Serial.println("[-] HTTP Request Failed: " + String(httpCode));
    }
    http.end();
}

// Setup Function
void setup() {
    Serial.begin(115200);
    Serial.println("\n[+] Starting ESP32-CAM...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("[-] WiFi Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] WiFi Connected: " + WiFi.localIP().toString());

    // Initialize Camera
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(Resolution::find(800, 600));
    cfg.setJpeg(80);

    if (!Camera.begin(cfg)) {
        Serial.println("[-] Camera Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] Camera Started");

    // Initialize NTP Time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Start AI Detection Timer
    xTaskCreate([](void*){
        while (1) {
            detectHelmet();
            delay(5000); // Capture Every 5 Seconds
        }
    }, "HelmetTask", 8192, NULL, 1, NULL);
}

// Main Loop
void loop() {
}
