#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#define SLAVE_ADDRESS 0x04

// Sensor Pins
#define TEMP_PIN 34
#define HUMIDITY_PIN 35
#define PRESSURE_PIN 32
#define LUX_PIN 33
#define WATER_LEVEL_PIN 36

// Terminal Block Pins
#define TERMINAL_BLOCK_1 2
#define TERMINAL_BLOCK_2 4

unsigned long sequenceNumber = 0;

// WiFi settings
const char* ssid = "AkitaSwitchSetup";
const char* password = "password";

AsyncWebServer server(80);

// Sensor Data
float temp = 0.0;
float humidity = 0.0;
float pressure = 0.0;
int lux = 0;
int waterLevel = 0;

void readSensors() {
  // Simulate sensor readings (replace with actual sensor code)
  temp = random(2000, 3500) / 100.0; // 20.0 to 35.0
  humidity = random(4000, 8000) / 100.0; // 40.0 to 80.0
  pressure = random(98000, 103000) / 100.0; // 980.0 to 1030.0
  lux = random(50, 200);
  waterLevel = analogRead(WATER_LEVEL_PIN);
}

void sendSensorData() {
  StaticJsonDocument doc;
  doc["type"] = "sensor_data";
  doc["timestamp"] = millis();
  doc["sequence"] = sequenceNumber++;
  doc["temp"] = temp;
  doc["humidity"] = humidity;
  doc["pressure"] = pressure;
  doc["lux"] = lux;
  doc["waterLevel"] = waterLevel;

  String jsonString;
  serializeJson(doc, jsonString);
  Wire.write(jsonString.c_str());
}

void receiveCommand(int howMany) {
  String receivedString = "";
  while (Wire.available()) {
    char c = Wire.read();
    receivedString += c;
  }

  StaticJsonDocument receivedDoc;
  DeserializationError error = deserializeJson(receivedDoc, receivedString);

  if (error) {
    Serial.println("JSON parse error!");
    return;
  }

  if (receivedDoc.containsKey("type") && receivedDoc["type"] == "command") {
    if (receivedDoc.containsKey("terminal")) {
      int terminal = receivedDoc["terminal"];
      if (receivedDoc.containsKey("action")) {
        String action = receivedDoc["action"];
        int pin;

        if (terminal == 1) {
          pin = TERMINAL_BLOCK_1;
        } else if (terminal == 2) {
          pin = TERMINAL_BLOCK_2;
        } else {
          Serial.println("Invalid terminal block.");
          return;
        }

        if (action == "on") {
          digitalWrite(pin, HIGH);
          Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" turned ON");
        } else if (action == "off") {
          digitalWrite(pin, LOW);
          Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" turned OFF");
        } else if (action == "reset") {
          digitalWrite(pin, LOW);
          Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" reset");
        }

        StaticJsonDocument ackDoc;
        ackDoc["type"] = "acknowledgment";
        ackDoc["sequence"] = receivedDoc["sequence"];
        ackDoc["timestamp"] = millis();
        ackDoc["status"] = "success";
        String ackString;
        serializeJson(ackDoc, ackString);
        Wire.write(ackString.c_str());
      }
    }
  }
}

void requestEvent() {
  readSensors();
  sendSensorData();
}

void setupWiFi() {
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("SoftAP IP address: ");
  Serial.println(IP);
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html = "<h1>Akita Switch Setup</h1>";
    html += "<p>Terminal Block 1: <a href='/terminal1/on'>ON</a> | <a href='/terminal1/off'>OFF</a> | <a href='/terminal1/reset'>RESET</a></p>";
    html += "<p>Terminal Block 2: <a href='/terminal2/on'>ON</a> | <a href='/terminal2/off'>OFF</a> | <a href='/terminal2/reset'>RESET</a></p>";
    html += "<p>Temp: " + String(temp) + " °C</p>";
    html += "<p>Humidity: " + String(humidity) + " %</p>";
    html += "<p>Pressure: " + String(pressure) + " hPa</p>";
    html += "<p>Lux: " + String(lux) + "</p>";
    html += "<p>Water Level: " + String(waterLevel) + "</p>";
    request->send(200, "text/html", html);
  });

  server.on("/terminal1/on", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_1, HIGH);
    request->send(200, "text/plain", "Terminal 1 ON");
  });

  server.on("/terminal1/off", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_1, LOW);
    request->send(200, "text/plain", "Terminal 1 OFF");
  });

  server.on("/terminal1/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_1, LOW);
    request->send(200, "text/plain", "Terminal 1 RESET");
  });

  server.on("/terminal2/on", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_2, HIGH);
    request->send(200, "text/plain", "Terminal 2 ON");
  });

  server.on("/terminal2/off", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_2, LOW);
    request->send(200, "text/plain", "Terminal 2 OFF");
  });

  server.on("/terminal2/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
    digitalWrite(TERMINAL_BLOCK_2, LOW);
    request->send(200, "text/plain", "Terminal 2 RESET");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SLAVE_ADDRESS);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveCommand);
  pinMode(TERMINAL_BLOCK_1, OUTPUT);
  pinMode(TERMINAL_BLOCK_2, OUTPUT);
  digitalWrite(TERMINAL_BLOCK_1, LOW);
  digitalWrite(TERMINAL_BLOCK_2, LOW);
  pinMode(WATER_LEVEL_PIN, INPUT);

  setupWiFi();
  setupWebServer();
}

void loop() {
  delay(1000);
}
