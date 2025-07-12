#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_eap_client.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>

// Eduroam WPA2-Enterprise credentials
#define EAP_ANONYMOUS_IDENTITY "anon@ucla.edu"
#define EAP_IDENTITY "your_email"
#define EAP_PASSWORD "your_password"
const char* ssid = "eduroam";

// Telegram Bot credentials
const char* botToken = "your token";
const int64_t chatID = 123; //your chat id

// Pins
const int ledPin = 23;
const int DIO = 21;
const int CLK = 22;

bool timerRunning = false;
unsigned long timerEndMillis = 0;
int countdownSeconds = 0;

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

  lastUpdateID = -1;
  checkTelegram();  // Skip old messages
}

void updateTimerDisplay() {
  if (timerRunning && millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();

    unsigned long currentMillis = millis();
    int remaining = (timerEndMillis - currentMillis) / 1000;

    if (remaining <= 0) {
      display.showNumberDecEx(0, 0b01000000, true);  // Show 00:00 with colon
      timerRunning = false;
      Serial.println("Timer done!");
      digitalWrite(ledPin, LOW);
    } else {
      int hours = remaining / 3600;
      int minutes = (remaining % 3600) / 60;
      int seconds = remaining % 60;

      if (hours > 0) {
        // Format H:MM (e.g., 1:45 as 145)
        int toDisplay = hours * 100 + minutes;
        display.showNumberDecEx(toDisplay, 0b01000000, true);  // colon ON
      } else {
        // Format MM:SS (e.g., 03:21 as 321)
        int toDisplay = minutes * 100 + seconds;
        display.showNumberDecEx(toDisplay, 0b01000000, true);  // colon ON
      }
    }
  }
}


int parseShorthandTime(String input) {
  input.toLowerCase();
  int hours = 0, minutes = 0, seconds = 0;
  String num = "";

  for (int i = 0; i < input.length(); i++) {
    char c = input[i];

    if (isDigit(c)) {
      num += c;  // Build the number
    } else if (c == 'h' || c == 'm' || c == 's') {
      int val = num.toInt();  // Convert built-up number string
      num = "";               // Reset the number string

      if (c == 'h') hours = val;
      else if (c == 'm') minutes = val;
      else if (c == 's') seconds = val;
    } else {
      num = ""; // Reset on any other char (like space or typo)
    }
  }

  return hours * 3600 + minutes * 60 + seconds;
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

  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: api.telegram.org\r\n" + "Connection: close\r\n\r\n");

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

      } else if (message == "/cancel") {
        if (timerRunning) {
          timerRunning = false;
          display.showNumberDecEx(0, 0b01000000, true);  // Show 00:00
          digitalWrite(ledPin, LOW);
          Serial.println("Timer cancelled.");
        } else {
          Serial.println("No active timer to cancel.");
        }
      } else if (message.startsWith("/timer")) {
        Serial.println("Parsing shorthand timer...");

        countdownSeconds = parseShorthandTime(message);
        if (countdownSeconds > 0) {
          if (timerRunning) {
            Serial.println("Cancelling previous timer...");
            timerRunning = false;
            display.showNumberDecEx(0, 0b01000000, true);  // Clear display with colon
            digitalWrite(ledPin, LOW);
          }

          timerEndMillis = millis() + (countdownSeconds * 1000);
          timerRunning = true;

          int hrs = countdownSeconds / 3600;
          int mins = (countdownSeconds % 3600) / 60;
          int secs = countdownSeconds % 60;

          Serial.printf("Starting timer for %d hr %d min %d sec\n", hrs, mins, secs);
          digitalWrite(ledPin, HIGH);
        } else {
          Serial.println("Could not parse shorthand time.");
        }
      }
    }
  }
}

void loop() {
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

  updateTimerDisplay();

  if (millis() - lastTelegramPoll >= telegramInterval) {
    lastTelegramPoll = millis();
    checkTelegram();
  }
}
