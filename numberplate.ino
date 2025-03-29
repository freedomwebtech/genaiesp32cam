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

// Firebase URL (Replace with your Firebase Realtime Database URL)
const char* FIREBASE_URL = "https://esp32yt-ec8f4-default-rtdb.firebaseio.com/data.json";

// NTP Server for Date and Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Base64 Encoding Function
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t length) {
    String encoded = "";
    int i = 0;
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

// Function to Send Data to Firebase
void sendDataToFirebase(String numberPlate, String dateTime, String imageBase64) {
    HTTPClient http;
    http.begin(FIREBASE_URL);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"number_plate\": \"" + numberPlate + "\",";
    payload += "\"date_time\": \"" + dateTime + "\",";
    payload += "\"image\": \"" + imageBase64 + "\"";
    payload += "}";
    
    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        Serial.println("[+] Firebase Response: " + http.getString());
    } else {
        Serial.println("[-] Firebase HTTP Request Failed: " + String(httpCode));
    }
    http.end();
}

// Function to Detect Vehicle Number Plate
void detectNumberPlate() {
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
    payload += ",{\"text\":\"Detect and extract the vehicle number plate text from this image. If a number plate is present, return only the plate number in plain text. If no plate is found, return 'No Plate'.\"}";
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
        String plateNumber = String(aiText);
        
        // Check if valid number plate is detected
        if (!aiText || plateNumber == "No Plate" || plateNumber.indexOf("I'm afraid") != -1 || 
            plateNumber.indexOf("unable to find") != -1 || plateNumber.indexOf("no plate") != -1 || 
            plateNumber.length() < 3) {
            Serial.println("[!] No valid number plate detected");
            return; // Don't send to Firebase if no valid plate is detected
        }
        
        // If we reach here, we have a valid number plate
        String dateTime = getCurrentTime();
        Serial.println("\n======= Vehicle Number Plate =======");
        Serial.println("📅 Date & Time: " + dateTime);
        Serial.println("🔢 Number Plate: " + plateNumber);
        Serial.println("====================================");
        
        // Send valid plate number to Firebase
        sendDataToFirebase(plateNumber, dateTime, base64Image);
        
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
    
    // Start Plate Detection Task
    xTaskCreate([](void*) {
        while (1) {
            detectNumberPlate();
            delay(3000); // Check every 3 seconds
        }
    }, "PlateTask", 8192, NULL, 1, NULL);
}

// Main Loop
void loop() {
}