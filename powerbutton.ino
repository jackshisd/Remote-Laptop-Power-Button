#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_eap_client.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>

// Eduroam WPA2-Enterprise credentials
#define EAP_ANONYMOUS_IDENTITY "anon@ucla.edu"
#define EAP_IDENTITY "jackmshi@ucla.edu"
#define EAP_PASSWORD "6217689aB!bruin"
const char* ssid = "eduroam";

// Telegram Bot credentials
const char* botToken = "7554571420:AAGJIfcGbK4DER0tSNLh65zR9trlYKXiiW8";
const int64_t chatID = 6069061615;

// Pins
const int ledPin = 23;
const int DIO = 21;
const int CLK = 22;

// Timer state
bool timerRunning = false;
unsigned long timerEndMillis = 0;
int countdownSeconds = 0;

// Timing control
unsigned long lastTelegramPoll = 0;
const unsigned long telegramInterval = 3000;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 1000;

int64_t lastUpdateID = -1;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 3600000;

TM1637Display display(CLK, DIO);

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.print(F("Connecting to network: "));
  Serial.println(ssid);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_ANONYMOUS_IDENTITY, EAP_IDENTITY, EAP_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }

  Serial.println("\nWiFi connected!");
  Serial.print(F("IP Address: "));
  Serial.println(WiFi.localIP());

  lastReconnectAttempt = millis();
  display.setBrightness(7);

  lastUpdateID = -1;  // Reset update tracking
  checkTelegram();    // Skip old messages
}

void updateTimerDisplay() {
  if (timerRunning && millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();

    unsigned long currentMillis = millis();
    int remaining = (timerEndMillis - currentMillis) / 1000;

    if (remaining <= 0) {
      display.showNumberDec(0, true);
      timerRunning = false;
      Serial.println("Timer done!");
      digitalWrite(ledPin, LOW);
    } else {
      int minutes = remaining / 60;
      int seconds = remaining % 60;
      int toDisplay = minutes * 100 + seconds;
      display.showNumberDec(toDisplay, true);
    }
  }
}

void checkTelegram() {
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connection failed");
    return;
  }

  String url = "/bot" + String(botToken) + "/getUpdates";
  if (lastUpdateID != -1) {
    url += "?offset=" + String(lastUpdateID + 1);
  }

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    updateTimerDisplay();
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String payload = client.readString();

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("Failed to parse JSON");
    return;
  }

  JsonArray results = doc["result"].as<JsonArray>();
  if (results.size() == 0) return;

  JsonObject messageObj;
  for (int i = results.size() - 1; i >= 0; i--) {
    if (results[i].containsKey("message") && results[i]["message"].containsKey("text")) {
      messageObj = results[i]["message"].as<JsonObject>();
      lastUpdateID = results[i]["update_id"];
      break;
    }
  }

  if (!messageObj.isNull()) {
    String message = messageObj["text"].as<String>();
    int64_t incomingChatID = messageObj["chat"]["id"].as<int64_t>();

    Serial.println("Received message: " + message);

    if (incomingChatID == chatID) {
      message.toLowerCase();

      if (message == "/ledon") {
        digitalWrite(ledPin, HIGH);
        Serial.println("LED ON");

      } else if (message == "/ledoff") {
        digitalWrite(ledPin, LOW);
        Serial.println("LED OFF");

      } else if (message.startsWith("/timer")) {
        Serial.println("Parsing timer...");

        int min = 0, sec = 0;
        int minIndex = message.indexOf("minute");
        if (minIndex != -1) {
          int space = message.lastIndexOf(' ', minIndex - 2);
          min = message.substring(space + 1, minIndex).toInt();
        }

        int secIndex = message.indexOf("second");
        if (secIndex != -1) {
          int space = message.lastIndexOf(' ', secIndex - 2);
          sec = message.substring(space + 1, secIndex).toInt();
        }

        countdownSeconds = min * 60 + sec;
        if (countdownSeconds > 0) {
          timerEndMillis = millis() + (countdownSeconds * 1000);
          timerRunning = true;
          Serial.printf("Starting timer for %d min %d sec\n", min, sec);
          digitalWrite(ledPin, HIGH);
        } else {
          Serial.println("Could not parse timer.");
        }
      }
    }
  }
}

void loop() {
  // Reconnect Wi-Fi if needed
  if (millis() - lastReconnectAttempt >= reconnectInterval || WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting to Wi-Fi...");
    WiFi.disconnect(true);
    WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_ANONYMOUS_IDENTITY, EAP_IDENTITY, EAP_PASSWORD);
    unsigned long connectStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 10000) {
      delay(500);
      updateTimerDisplay();
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected!");
    } else {
      Serial.println("\nReconnection failed.");
    }
    lastReconnectAttempt = millis();
  }

  // Update timer display
  updateTimerDisplay();

  // Poll Telegram every 3 seconds
  if (!timerRunning && millis() - lastTelegramPoll >= telegramInterval) {
    lastTelegramPoll = millis();
    checkTelegram();
  }
}
