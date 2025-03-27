#include <esp32cam.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi Credentials
static const char* WIFI_SSID = "apex";
static const char* WIFI_PASS = "freedomtech";

// Gemini AI API Key
static const char* GEMINI_API_KEY = "AIzaSyCl6HMflHkt3O-tJZhc8ITgVDwwPf-oH_Y";

WebServer server(80);

// Web Page with Chat Interface
static const char FRONTPAGE[] = R"EOT(
<!doctype html>
<html>
<head>
    <title>ESP32-CAM AI Chatbot</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; }
        img { max-width: 100%; border: 1px solid #ccc; }
        input, button { padding: 10px; font-size: 16px; margin: 10px; }
        button { background-color: #4CAF50; color: white; border: none; cursor: pointer; }
        button:hover { background-color: #45a049; }
        #chatBox { width: 80%; height: 300px; border: 1px solid #ddd; overflow-y: auto; padding: 10px; text-align: left; }
        #clearChatBtn { background-color: red; }
    </style>
</head>
<body>
    <h1>ESP32-CAM AI Chatbot</h1>
    <h2>Live Feed</h2>
    <img id="liveFeed" src="/mjpeg" alt="Live Stream"><br>

    <h2>Ask AI About Live Feed</h2>
    <div id="chatBox"></div>
    <input type="text" id="userInput" placeholder="Ask AI about the live feed...">
    <button id="askBtn">Ask</button>
    <button id="clearChatBtn">Clear Chat</button>

    <script>
        document.getElementById('askBtn').addEventListener('click', async () => {
            const userInput = document.getElementById('userInput').value;
            if (!userInput) return;

            const chatBox = document.getElementById('chatBox');
            chatBox.innerHTML += `<p><b>You:</b> ${userInput}</p>`;
            document.getElementById('userInput').value = "Capturing image...";

            // Show "Processing..." message
            chatBox.innerHTML += `<p><b>AI:</b> Processing...</p>`;

            // Stop the live stream before capturing an image
            document.getElementById('liveFeed').src = "";

            try {
                const response = await fetch('/chat_ai?query=' + encodeURIComponent(userInput));
                if (!response.ok) throw new Error('Server error: ' + response.status);
                const aiResponse = await response.text();

                // Remove "Processing..." message
                chatBox.innerHTML = chatBox.innerHTML.replace(`<p><b>AI:</b> Processing...</p>`, "");

                chatBox.innerHTML += `<p><b>AI:</b> ${aiResponse}</p>`;
            } catch (err) {
                chatBox.innerHTML += `<p><b>AI:</b> Error: ${err.message}</p>`;
            }

            // Restart the live stream after getting AI response
            document.getElementById('liveFeed').src = "/mjpeg?" + new Date().getTime();
            document.getElementById('userInput').value = "";
        });

        // Clear Chat History
        document.getElementById('clearChatBtn').addEventListener('click', () => {
            document.getElementById('chatBox').innerHTML = "";
        });
    </script>
</body>
</html>
)EOT";

// AI Chat Function
static void chatWithAI() {
  if (!server.hasArg("query")) {
    server.send(400, "text/plain", "Missing query parameter");
    return;
  }

  String userQuery = server.arg("query");
  Serial.println("User Question: " + userQuery);

  // Capture frame
  auto frame = esp32cam::capture();
  if (frame == nullptr) {
    Serial.println("Capture failed");
    server.send(500, "text/plain", "Capture failed");
    return;
  }

  // Encode to Base64
  String base64Image = base64_encode(frame->data(), frame->size());

  // Send to Gemini AI
  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + String(GEMINI_API_KEY);
  http.begin(url);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"contents\":[{";
  payload += "\"parts\":["; 
  payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}},";
  payload += "{\"text\":\"" + userQuery + "\"}";
  payload += "]}]}";

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Gemini response: " + response);

    // Parse JSON
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      server.send(500, "text/plain", "JSON parse error");
      return;
    }

    const char* text = doc["candidates"][0]["content"]["parts"][0]["text"];
    if (text) {
      server.send(200, "text/plain", text);
    } else {
      server.send(200, "text/plain", "No relevant answer found");
    }
  } else {
    server.send(500, "text/plain", "HTTP request failed");
  }
  http.end();
}

// Serve MJPEG Stream
static void serveMjpeg() {
  Serial.println("MJPEG streaming begin");
  WiFiClient client = server.client();
  int nFrames = esp32cam::Camera.streamMjpeg(client);
  Serial.printf("MJPEG streaming end: %d frames\n", nFrames);
}

// Request Handlers
void addRequestHandlers() {
  server.on("/", HTTP_GET, [] {
    server.send(200, "text/html", FRONTPAGE);
  });

  server.on("/mjpeg", HTTP_GET, serveMjpeg);
  server.on("/chat_ai", HTTP_GET, chatWithAI);
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi failed");
    delay(5000);
    ESP.restart();
  }
  Serial.println("WiFi connected");

  using namespace esp32cam;
  Config cfg;
  cfg.setPins(pins::AiThinker);
  cfg.setResolution(Resolution::find(800, 600));
  cfg.setJpeg(80);

  if (!Camera.begin(cfg)) {
    Serial.println("Camera failed");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Camera started");

  Serial.print("http://");
  Serial.println(WiFi.localIP());

  addRequestHandlers();
  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();
}

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
