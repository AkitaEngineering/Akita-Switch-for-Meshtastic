/**
 * Akita Switch for Meshtastic - Sensor Module Firmware (v4 - Ready for Sensors)
 *
 * Copyright (C) 2025 Akita Engineering <info@akitaengineering.com>
 * Website: https://www.akitaengineering.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * --- Description ---
 * v4: Ready for real sensor integration, added basic web auth, OTA password option,
 * improved web styling, placeholders for power management.
 * Includes configuration persistence (SPIFFS), OTA updates, web config interface.
 *
 * Runs on an ESP32, acts as an I2C slave.
 * - Reads (simulated or real) sensors.
 * - Controls digital output pins (terminal blocks).
 * - Hosts a WiFi SoftAP for initial configuration OR connects to a configured WiFi network.
 * - Provides a web server for status, control, and configuration (with basic auth).
 * - Supports Over-the-Air (OTA) firmware updates (with optional password).
 * - Communicates with an I2C master (akita_meshtastic.py script) using JSON.
 * - Implements sequence number checking for I2C commands.
 */

// --- Libraries ---
#include <Arduino.h>
#include <Wire.h>           // I2C communication
#include <ArduinoJson.h>    // JSON handling (v6+)
#include <WiFi.h>           // ESP32 WiFi
#include <ESPAsyncWebServer.h> // Asynchronous Web Server
#include <AsyncTCP.h>       // Required by ESPAsyncWebServer
#include <SPIFFS.h>         // ESP32 Flash File System for configuration
#include <ArduinoOTA.h>     // Over-the-Air Updates

// --- Optional Sensor Libraries (Uncomment needed ones) ---
// #include <DHT.h>            // For DHT11, DHT22 sensors
// #include <Adafruit_Sensor.h> // Required by BME280/BMP280
// #include <Adafruit_BME280.h> // For BME280 Temp/Humidity/Pressure sensor (I2C/SPI)
// #include <Adafruit_BMP280.h> // For BMP280 Temp/Pressure sensor (I2C/SPI)
// #include <BH1750.h>         // For BH1750 Light sensor (I2C)

// --- Configuration Constants ---
#define CONFIG_FILE "/config.json" // Path for configuration file on SPIFFS
#define DEFAULT_I2C_ADDRESS 0x04   // Default I2C address if config fails
#define MAX_I2C_BUFFER 128         // Max bytes for I2C send buffer (Wire buffer size)
#define DEFAULT_HOSTNAME "akita-switch" // Hostname for OTA and network identification
#define WEB_ADMIN_USER "admin"     // Username for web config page authentication
#define WEB_ADMIN_PASS "akita"     // Default Password for web config - CHANGE THIS!

// --- Optional Power Management ---
// #define ENABLE_DEEP_SLEEP      // Uncomment to enable deep sleep logic placeholders
#ifdef ENABLE_DEEP_SLEEP
  #define DEEP_SLEEP_SECONDS 300 // Example: Sleep for 5 minutes (300 seconds)
  RTC_DATA_ATTR int bootCount = 0; // Example: Variable stored in RTC memory during deep sleep
#endif

// Sensor Pins (Update if using different pins and uncomment defines)
// #define DHT_PIN 34              // Example: Pin for a DHT sensor
// #define BME_I2C_ADDRESS 0x76    // Example: I2C Address for BME280 (or 0x77)
// #define BH1750_I2C_ADDRESS 0x23 // Example: I2C Address for BH1750 (or 0x5C)
#define WATER_LEVEL_PIN 36     // Example: Analog pin for water level sensor

// Terminal Block Output Pins (Update if using different pins)
#define TERMINAL_BLOCK_1_PIN 2
#define TERMINAL_BLOCK_2_PIN 4

// --- Global Variables ---

// Configuration Structure
struct Configuration {
  char wifi_ssid[33] = "";
  char wifi_password[65] = "";
  uint8_t i2c_address = DEFAULT_I2C_ADDRESS;
  char ota_hostname[33] = DEFAULT_HOSTNAME;
  char web_password[33] = WEB_ADMIN_PASS; // Store web password (max 32 chars)
  // char ota_password[33] = ""; // Optional: Add OTA password persistence
};
Configuration config;

// Web Server
AsyncWebServer server(80);

// I2C Communication State
char i2cSendBuffer[MAX_I2C_BUFFER];
volatile bool isAckPending = false;
volatile unsigned long lastExecutedSequence = (unsigned long)-1;
unsigned long currentSensorSequenceNumber = 0;

// Sensor Data Storage
float temp = -999.9;
float humidity = -999.9;
float pressure = -9999.9;
int lux = -1;
int waterLevel = -1;

// Sensor Object Placeholders (Uncomment and use needed ones)
// DHT dht(DHT_PIN, DHT22); // Example: DHT sensor object
// Adafruit_BME280 bme;      // Example: BME280 sensor object (I2C default)
// BH1750 lightMeter;       // Example: BH1750 sensor object (I2C default addr 0x23)

// WiFi State
bool wifiConnected = false;
bool apMode = false;
const char* AP_SSID_PREFIX = "AkitaSwitchSetup-";

#ifdef ENABLE_DEEP_SLEEP
volatile unsigned long lastActivityMillis = 0;
volatile unsigned long lastI2CRequestMillis = 0;
const unsigned long INACTIVITY_BEFORE_SLEEP_MS = 30000UL; // 30s before entering deep sleep
bool skipFullSetupOnWake = false;
#endif

// --- Function Declarations ---
// (Declarations as before)
void loadConfiguration();
void saveConfiguration();
void setupWiFi();
void startSoftAP();
void connectToWiFi();
void setupWebServer();
void setupOTA();
void readSensors();
void prepareSensorDataForI2C();
void prepareAckDataForI2C(unsigned long sequence, const char* status, const char* errorCode = nullptr);
void receiveI2CCommand(int howMany);
void requestI2CData();
void goToDeepSleep();

// --- Configuration Handling (SPIFFS) ---
/**
 * @brief Loads configuration from SPIFFS, now includes web password.
 */
void loadConfiguration() {
  if (!SPIFFS.begin(true)) {
    Serial.println("ERROR: Failed to mount SPIFFS. Using default config.");
    config.i2c_address = DEFAULT_I2C_ADDRESS;
    strncpy(config.ota_hostname, DEFAULT_HOSTNAME, sizeof(config.ota_hostname) - 1);
    config.ota_hostname[sizeof(config.ota_hostname) - 1] = '\0';
    strncpy(config.web_password, WEB_ADMIN_PASS, sizeof(config.web_password) - 1);
    config.web_password[sizeof(config.web_password) - 1] = '\0';
    return;
  }

  if (SPIFFS.exists(CONFIG_FILE)) {
    File configFile = SPIFFS.open(CONFIG_FILE, "r");
    if (!configFile) {
        Serial.println("ERROR: Failed to open config file. Using defaults.");
        // Apply defaults explicitly here too
        config.i2c_address = DEFAULT_I2C_ADDRESS;
        strncpy(config.ota_hostname, DEFAULT_HOSTNAME, sizeof(config.ota_hostname) - 1);
        config.ota_hostname[sizeof(config.ota_hostname) - 1] = '\0';
        strncpy(config.web_password, WEB_ADMIN_PASS, sizeof(config.web_password) - 1);
        config.web_password[sizeof(config.web_password) - 1] = '\0';
        return;
    }
    StaticJsonDocument<512> doc; // Increased size slightly for web password
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    if (error) {
        Serial.print("ERROR: Failed to parse config file: "); Serial.println(error.c_str());
        Serial.println("Using default config and attempting to save defaults.");
        config.i2c_address = DEFAULT_I2C_ADDRESS;
        strncpy(config.ota_hostname, DEFAULT_HOSTNAME, sizeof(config.ota_hostname) - 1);
        config.ota_hostname[sizeof(config.ota_hostname) - 1] = '\0';
        strncpy(config.web_password, WEB_ADMIN_PASS, sizeof(config.web_password) - 1);
        config.web_password[sizeof(config.web_password) - 1] = '\0';
        saveConfiguration(); // Attempt to fix the config file
        return;
    }

    strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
    strlcpy(config.wifi_password, doc["wifi_password"] | "", sizeof(config.wifi_password));
    config.i2c_address = doc["i2c_address"] | DEFAULT_I2C_ADDRESS;
    strlcpy(config.ota_hostname, doc["ota_hostname"] | DEFAULT_HOSTNAME, sizeof(config.ota_hostname));
    // Load web password, use default if not found
    strlcpy(config.web_password, doc["web_password"] | WEB_ADMIN_PASS, sizeof(config.web_password));

    Serial.println("Configuration loaded successfully:");
    Serial.printf("  WiFi SSID: %s\n", config.wifi_ssid);
    Serial.printf("  I2C Addr : 0x%02X\n", config.i2c_address);
    Serial.printf("  Hostname : %s\n", config.ota_hostname);
    // Don't print passwords to serial
  } else {
    Serial.println("Config file not found. Using default config and saving defaults.");
    config.i2c_address = DEFAULT_I2C_ADDRESS;
    strncpy(config.ota_hostname, DEFAULT_HOSTNAME, sizeof(config.ota_hostname) - 1);
    config.ota_hostname[sizeof(config.ota_hostname) - 1] = '\0';
    strncpy(config.web_password, WEB_ADMIN_PASS, sizeof(config.web_password) - 1);
    config.web_password[sizeof(config.web_password) - 1] = '\0';
    saveConfiguration();
  }
}

/**
 * @brief Saves configuration to SPIFFS, now includes web password.
 */
void saveConfiguration() {
  if (!SPIFFS.begin(true)) { Serial.println("ERROR: Failed to mount SPIFFS for saving config."); return; }
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (!configFile) { Serial.println("ERROR: Failed to open config file for writing."); return; }
  StaticJsonDocument<512> doc;
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_password"] = config.wifi_password;
  doc["i2c_address"] = config.i2c_address;
  doc["ota_hostname"] = config.ota_hostname;
  doc["web_password"] = config.web_password; // Save web password
  if (serializeJson(doc, configFile) == 0) { Serial.println("ERROR: Failed to write to config file."); }
  else { Serial.println("Configuration saved successfully."); }
  configFile.close();
}

// --- WiFi Handling ---
/**
 * @brief Sets up WiFi. Tries STA, falls back to SoftAP. Enables OTA if STA connects.
 */
void setupWiFi() {
  WiFi.mode(WIFI_STA); // Start in Station mode
  wifiConnected = false;
  apMode = false;

  if (strlen(config.wifi_ssid) > 0) {
    Serial.printf("Attempting to connect to WiFi SSID: %s\n", config.wifi_ssid);
    WiFi.setHostname(config.ota_hostname); // Set hostname before connecting
    WiFi.begin(config.wifi_ssid, config.wifi_password);

    // Wait for connection (with timeout)
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) { // 15 second timeout
      Serial.print(".");
      delay(500);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println("WiFi connected!");
      Serial.print("IP Address: "); Serial.println(WiFi.localIP());
      Serial.print("Hostname  : "); Serial.println(WiFi.getHostname());
      // Setup OTA now that we are connected
      setupOTA();
    } else {
      Serial.println("Failed to connect to configured WiFi.");
      WiFi.disconnect(true); // Ensure disconnected before starting AP
      delay(100); // Short delay
      startSoftAP();
    }
  } else {
    Serial.println("WiFi SSID not configured.");
    startSoftAP();
  }
}

/**
 * @brief Starts the SoftAP mode for configuration.
 */
void startSoftAP() {
  apMode = true;
  wifiConnected = false;
  Serial.println("Starting SoftAP for configuration...");
  WiFi.mode(WIFI_AP); // Set mode explicitly to AP

  // Create a unique AP name based on MAC address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String apSSID = AP_SSID_PREFIX + String(mac[4], HEX) + String(mac[5], HEX);

  Serial.printf("SoftAP SSID: %s\n", apSSID.c_str());
  // Use a default password for initial setup AP
  if (!WiFi.softAP(apSSID.c_str(), "password")) { // Simple default password - CHANGE THIS if needed
      Serial.println("ERROR: Failed to start SoftAP!");
      // Consider fallback action? Reboot?
      return;
  }

  IPAddress IP = WiFi.softAPIP();
  Serial.print("SoftAP IP address: http://"); Serial.println(IP);
}

// --- OTA Update Handling ---
/**
 * @brief Configures OTA, now with optional password from config (if added).
 */
void setupOTA() {
  if (!wifiConnected) return; // Only run OTA if connected to WiFi

  ArduinoOTA.setHostname(config.ota_hostname);

  // --- Optional: Set OTA Password ---
  // Uncomment the next line and potentially add ota_password to the Configuration struct
  // and handle it in load/saveConfiguration and the web config page if you want password protection.
  // ArduinoOTA.setPassword(config.ota_password);
  // ArduinoOTA.setPassword("your_ota_password"); // Or hardcode if preferred

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
      Serial.println("OTA Start updating " + type);
      // Optional: Stop sensors, I2C, etc. during update
      // Wire.end(); // Example: Stop I2C
    })
    .onEnd([]() {
      Serial.println("\nOTA End");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  Serial.println("OTA Update Service Ready.");
}


// --- Sensor Reading ---
/**
 * @brief Reads sensor values. Integrate your actual sensor code here.
 */
void readSensors() {
  // --- START SENSOR SIMULATION (DELETE OR COMMENT OUT WHEN USING REAL SENSORS) ---
  temp = random(1500, 3000) / 100.0;       // 15.0 to 30.0 °C
  humidity = random(3000, 7500) / 100.0;   // 30.0 to 75.0 %
  pressure = random(97000, 102500) / 100.0; // 970.0 to 1025.0 hPa
  lux = random(10, 800);                   // 10 to 800 lux
  // --- END SENSOR SIMULATION ---

  // --- Read REAL Sensors (Examples below, uncomment and adapt) ---
  // Ensure necessary libraries are included and sensors initialized in setup()

  // Example 1: Analog Water Level (already using WATER_LEVEL_PIN)
  waterLevel = analogRead(WATER_LEVEL_PIN);

  // Example 2: DHT Temperature/Humidity Sensor
  /*
  if (dht) { // Check if dht object was initialized (use appropriate check)
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();
    // Check if any reads failed (returns NaN)
    if (!isnan(newTemp)) { temp = newTemp; } else { Serial.println("Failed to read temp from DHT"); temp = -999.9; }
    if (!isnan(newHum)) { humidity = newHum; } else { Serial.println("Failed to read humidity from DHT"); humidity = -999.9; }
  }
  */

  // Example 3: BME280 I2C Temperature/Humidity/Pressure Sensor
  /*
  // Assuming 'bme' object is initialized in setup()
  float newTemp = bme.readTemperature();
  float newHum = bme.readHumidity();
  float newPress = bme.readPressure() / 100.0F; // Convert Pa to hPa
  if (!isnan(newTemp)) { temp = newTemp; } else { temp = -999.9; }
  if (!isnan(newHum)) { humidity = newHum; } else { humidity = -999.9; }
  if (!isnan(newPress)) { pressure = newPress; } else { pressure = -9999.9; }
  */

 // Example 4: BH1750 I2C Light Sensor
 /*
  // Assuming 'lightMeter' object is initialized in setup()
  // Check if sensor needs waking up or if data is ready depending on mode
  if (lightMeter.measurementReady()) {
    float newLux = lightMeter.readLightLevel();
    if (newLux >= 0) { lux = (int)newLux; } else { Serial.println("Failed to read light level"); lux = -1; }
  }
 */
  // --- End REAL Sensor Code ---

  Serial.printf("Read Sensors: T=%.1fC, H=%.1f%%, P=%.1fhPa, L=%d, W=%d\n",
                temp, humidity, pressure, lux, waterLevel);
}

// --- I2C Communication ---
/**
 * @brief Prepares sensor data JSON string for sending via I2C onRequest.
 */
void prepareSensorDataForI2C() {
  StaticJsonDocument<256> doc; // Adjust size if more sensors/precision needed
  doc["type"] = "sensor_data";
  doc["timestamp"] = millis(); // Use ESP32's millis() as timestamp
  doc["sequence"] = currentSensorSequenceNumber++;
  // Only include valid readings (check against initial error values)
  if (temp > -999.0) doc["temp"] = temp;
  if (humidity > -999.0) doc["humidity"] = humidity;
  if (pressure > -9999.0) doc["pressure"] = pressure;
  if (lux >= 0) doc["lux"] = lux;
  if (waterLevel >= 0) doc["waterLevel"] = waterLevel;
  String jsonString;
  serializeJson(doc, jsonString); // Serialize the JSON document to a String
  if (jsonString.length() < MAX_I2C_BUFFER) {
      strncpy(i2cSendBuffer, jsonString.c_str(), MAX_I2C_BUFFER - 1);
      i2cSendBuffer[jsonString.length()] = '\0'; // Ensure null termination
      Serial.print("Prepared Sensor Data for I2C: "); Serial.println(i2cSendBuffer);
  } else {
      Serial.println("ERROR: Sensor JSON too long for I2C buffer!");
      snprintf(i2cSendBuffer, MAX_I2C_BUFFER, "{\"type\":\"error\", \"msg\":\"sensor JSON too long\"}");
  }
  isAckPending = false; // Data prepared is sensor data, not an acknowledgment
}

/**
 * @brief Prepares acknowledgment JSON string for sending via I2C onRequest.
 */
void prepareAckDataForI2C(unsigned long sequence, const char* status, const char* errorCode = nullptr) {
    StaticJsonDocument<128> ackDoc; // Acks should be small
    ackDoc["type"] = "acknowledgment";
    ackDoc["sequence"] = sequence;
    ackDoc["timestamp"] = millis();
    ackDoc["status"] = status;
    if (errorCode) {
        ackDoc["error_code"] = errorCode;
    }
    String ackString;
    serializeJson(ackDoc, ackString); // Serialize to String
    if (ackString.length() < MAX_I2C_BUFFER) {
        strncpy(i2cSendBuffer, ackString.c_str(), MAX_I2C_BUFFER - 1);
        i2cSendBuffer[ackString.length()] = '\0'; // Ensure null termination
        Serial.print("Prepared ACK for I2C: "); Serial.println(i2cSendBuffer);
        isAckPending = true; // Set flag: an acknowledgment is ready to be sent
    } else {
         Serial.println("ERROR: ACK JSON too long for I2C buffer!");
         snprintf(i2cSendBuffer, MAX_I2C_BUFFER, "{\"type\":\"error\", \"msg\":\"ack JSON too long\"}");
         isAckPending = true; // Still flag it as pending, even though it's an error message
    }
}

/**
 * @brief I2C Event Handler: Called when the I2C master sends data (onReceive).
 */
void receiveI2CCommand(int howMany) {
  Serial.printf("I2C Receive Event: %d bytes received.\n", howMany);
  #ifdef ENABLE_DEEP_SLEEP
    lastActivityMillis = millis();
  #endif
  if (howMany <= 0 || howMany >= MAX_I2C_BUFFER) {
      Serial.printf("I2C Error: Invalid number of bytes received (%d).\n", howMany);
      while (Wire.available()) { Wire.read(); } // Clear buffer
      prepareAckDataForI2C(0, "error", "I2C_READ_LENGTH_ERROR");
      return;
  }
  char receivedBytes[MAX_I2C_BUFFER];
  int bytesRead = 0;
  while (Wire.available() && bytesRead < howMany && bytesRead < MAX_I2C_BUFFER -1) {
    receivedBytes[bytesRead++] = Wire.read();
  }
  receivedBytes[bytesRead] = '\0'; // Null-terminate the string
  while (Wire.available()) { Wire.read(); } // Discard any extra unexpected bytes
  Serial.print("I2C Received Raw String: "); Serial.println(receivedBytes);

  StaticJsonDocument<256> receivedDoc; // Adjust size if commands can be large
  DeserializationError error = deserializeJson(receivedDoc, receivedBytes, bytesRead);
  unsigned long receivedSequence = receivedDoc["sequence"] | (lastExecutedSequence + 1); // Default if parse fails

  if (error) {
    Serial.print("I2C JSON parse error: "); Serial.println(error.c_str());
    if (strstr(receivedBytes, "\"sequence\"")) { receivedSequence = receivedDoc["sequence"] | 0; } else { receivedSequence = 0; } // Try to get sequence if possible
    prepareAckDataForI2C(receivedSequence, "error", "JSON_PARSE_ERROR");
    return;
  }
  if (!receivedDoc.containsKey("type") || !receivedDoc["type"].is<const char*>() || strcmp(receivedDoc["type"], "command") != 0) {
       Serial.println("I2C Error: Invalid message type or missing 'type'. Expected 'command'.");
       prepareAckDataForI2C(receivedSequence, "error", "INVALID_TYPE"); return;
  }
  if (!receivedDoc.containsKey("sequence") || !receivedDoc["sequence"].is<unsigned long>()) {
       Serial.println("I2C Error: Missing or invalid 'sequence'.");
       prepareAckDataForI2C(0, "error", "MISSING_OR_INVALID_SEQUENCE"); return;
  }
  receivedSequence = receivedDoc["sequence"].as<unsigned long>(); // Get valid sequence

  if (receivedSequence == lastExecutedSequence) {
      Serial.print("Duplicate sequence received: "); Serial.println(receivedSequence);
      prepareAckDataForI2C(receivedSequence, "success", "DUPLICATE_IGNORED"); return; // Acknowledge but don't re-execute
  }

  if (receivedDoc.containsKey("terminal") && receivedDoc["terminal"].is<int>() &&
      receivedDoc.containsKey("action") && receivedDoc["action"].is<const char*>()) {
      int terminal = receivedDoc["terminal"]; const char* action = receivedDoc["action"]; int pin = -1;
      if (terminal == 1) pin = TERMINAL_BLOCK_1_PIN; else if (terminal == 2) pin = TERMINAL_BLOCK_2_PIN;
      else { Serial.print("Invalid terminal block number: "); Serial.println(terminal); prepareAckDataForI2C(receivedSequence, "error", "INVALID_TERMINAL"); return; }

      if (strcmp(action, "on") == 0) { digitalWrite(pin, HIGH); Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" turned ON via I2C"); }
      else if (strcmp(action, "off") == 0) { digitalWrite(pin, LOW); Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" turned OFF via I2C"); }
      else if (strcmp(action, "reset") == 0) { digitalWrite(pin, LOW); Serial.print("Terminal block "); Serial.print(terminal); Serial.println(" RESET (turned OFF) via I2C"); } // Reset = OFF
      else { Serial.print("Invalid action: "); Serial.println(action); prepareAckDataForI2C(receivedSequence, "error", "INVALID_ACTION"); return; }
      lastExecutedSequence = receivedSequence; // Update only after successful execution
      prepareAckDataForI2C(receivedSequence, "success"); // Prepare success ACK
  } else {
       Serial.println("I2C Error: Missing 'terminal'/'action' or invalid format.");
       prepareAckDataForI2C(receivedSequence, "error", "MISSING_OR_INVALID_PARAMS");
  }
}

/**
 * @brief I2C Event Handler: Called when the I2C master requests data (onRequest).
 */
void requestI2CData() {
  #ifdef ENABLE_DEEP_SLEEP
    lastActivityMillis = millis();
    lastI2CRequestMillis = millis();
  #endif
  Serial.print("I2C Request Event. Sending: ");
  if (!isAckPending) {
      readSensors(); // Read fresh data before sending
      prepareSensorDataForI2C();
      Serial.println("(Sensor Data)");
  } else {
      Serial.println("(Pending ACK)");
  }
  Wire.write(i2cSendBuffer); // Send buffer content as C-string
  // Do NOT clear isAckPending here, allow master to retry reading ACK if needed
}

// --- Web Server Setup ---
/**
 * @brief Configures web server, adding basic auth to config page.
 */
void setupWebServer() {
  // Basic CSS (Improved Styling)
  const char* basicCSS = R"rawliteral(
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; margin: 0; padding: 0; background-color: #f8f9fa; color: #343a40; }
.container { max-width: 800px; margin: 20px auto; padding: 20px; background-color: #ffffff; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.05); }
h1, h2 { color: #0056b3; border-bottom: 2px solid #dee2e6; padding-bottom: 8px; margin-top: 1.5em; margin-bottom: 1em; }
h1:first-child, h2:first-child { margin-top: 0; }
nav { margin-bottom: 20px; background-color: #e9ecef; padding: 12px 20px; border-radius: 5px; }
nav a { margin-right: 20px; text-decoration: none; color: #0056b3; font-weight: 500; font-size: 1.1em; }
nav a:hover { text-decoration: underline; color: #003d80; }
.card { background-color: #fff; padding: 20px; border-radius: 5px; margin-bottom: 20px; border: 1px solid #e0e0e0; }
.button { display: inline-block; margin: 5px 5px 5px 0; padding: 10px 18px; color: white; text-decoration: none; border-radius: 5px; font-weight: 500; border: none; cursor: pointer; transition: background-color 0.2s ease; }
.button:hover { opacity: 0.9; }
.on { background-color: #28a745; } .off { background-color: #dc3545; }
.save { background-color: #007bff; } .reboot { background-color: #ffc107; color: #212529; }
.state { font-weight: bold; padding: 3px 8px; border-radius: 4px; color: white; margin-left: 10px; font-size: 0.9em; vertical-align: middle;}
.state-on { background-color: #28a745; } .state-off { background-color: #dc3545; }
label { display: block; margin-bottom: 6px; font-weight: 500; color: #495057; }
input[type=text], input[type=password], input[type=number] { width: calc(100% - 24px); padding: 10px; margin-bottom: 15px; border: 1px solid #ced4da; border-radius: 4px; font-size: 1em; }
input:focus { border-color: #80bdff; outline: 0; box-shadow: 0 0 0 0.2rem rgba(0,123,255,.25); }
.msg { padding: 12px 15px; border-radius: 5px; margin-top: 15px; border: 1px solid transparent; }
.success { background-color: #d1e7dd; color: #0f5132; border-color: #badbcc; }
.error { background-color: #f8d7da; color: #842029; border-color: #f5c2c7; }
footer { margin-top: 30px; text-align: center; font-size: 0.9em; color: #6c757d; border-top: 1px solid #e0e0e0; padding-top: 15px; }
</style>
)rawliteral";

  // Helper to build HTML structure (Updated)
  auto buildHtml = [&](const String& title, const String& bodyContent) {
    String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>";
    html += title; html += " - Akita Switch</title>"; html += "<meta name='viewport' content='width=device-width, initial-scale=1'>"; html += basicCSS; html += "</head><body><div class='container'>";
    html += "<nav><a href='/'>Status</a> | <a href='/config'>Configure</a></nav>"; html += "<h1>"; html += title; html += "</h1>"; html += bodyContent;
    html += "<footer>Akita Switch Firmware | ";
    if(apMode) html += "Mode: SoftAP | IP: " + WiFi.softAPIP().toString(); else if(wifiConnected) html += "Mode: WiFi Client | IP: " + WiFi.localIP().toString(); else html += "Mode: Disconnected";
    html += "</footer></div></body></html>"; return html;
  };

  // --- Web Handlers ---

  // Main Status page (GET) - No Authentication needed
  server.on("/", HTTP_GET, [buildHtml](AsyncWebServerRequest *request){
    readSensors(); // Update sensor values
    #ifdef ENABLE_DEEP_SLEEP
      lastActivityMillis = millis();
    #endif
    String body = "<div class='card'><h2>Terminal Blocks</h2>";
    int term1State = digitalRead(TERMINAL_BLOCK_1_PIN); int term2State = digitalRead(TERMINAL_BLOCK_2_PIN);
    body += "<p>Terminal Block 1 <span class='state " + String(term1State == HIGH ? "state-on'>ON" : "state-off'>OFF") + "</span>"; body += "<a href='/terminal/1/on' class='button on'>ON</a><a href='/terminal/1/off' class='button off'>OFF</a></p>";
    body += "<p>Terminal Block 2 <span class='state " + String(term2State == HIGH ? "state-on'>ON" : "state-off'>OFF") + "</span>"; body += "<a href='/terminal/2/on' class='button on'>ON</a><a href='/terminal/2/off' class='button off'>OFF</a></p></div>";
    body += "<div class='card'><h2>Sensor Readings</h2>"; body += "<p>Temperature: " + (temp > -999.0 ? String(temp, 1) + " &deg;C" : "N/A") + "</p>"; body += "<p>Humidity: " + (humidity > -999.0 ? String(humidity, 1) + " %" : "N/A") + "</p>"; body += "<p>Pressure: " + (pressure > -9999.0 ? String(pressure, 1) + " hPa" : "N/A") + "</p>"; body += "<p>Light: " + (lux >= 0 ? String(lux) + " lux" : "N/A") + "</p>"; body += "<p>Water Level: " + (waterLevel >= 0 ? String(waterLevel) : "N/A") + "</p></div>";
    request->send(200, "text/html", buildHtml("Status", body));
   });

  // Terminal control handler (GET, redirects) - No Authentication needed for basic control
  server.on("^\\/terminal\\/([1-2])\\/(on|off|reset)$", HTTP_GET, [](AsyncWebServerRequest *request){
    int terminal = request->pathArg(0).toInt(); String action = request->pathArg(1); int pin = -1; String responseText = ""; if (terminal == 1) pin = TERMINAL_BLOCK_1_PIN; else if (terminal == 2) pin = TERMINAL_BLOCK_2_PIN;
    if (pin != -1) { if (action == "on") { digitalWrite(pin, HIGH); responseText = "T" + String(terminal) + " ON"; } else if (action == "off") { digitalWrite(pin, LOW); responseText = "T" + String(terminal) + " OFF"; } else if (action == "reset") { digitalWrite(pin, LOW); responseText = "T" + String(terminal) + " RESET"; } 
      Serial.println(responseText + " via Web");
      #ifdef ENABLE_DEEP_SLEEP
        lastActivityMillis = millis();
      #endif
      request->redirect("/"); }
    else { request->send(400, "text/plain", "Invalid Terminal"); }
  });

  // Configuration Page (GET) - Add Authentication
  server.on("/config", HTTP_GET, [buildHtml](AsyncWebServerRequest *request){
    // --- Add Basic Authentication ---
    if(!request->authenticate(WEB_ADMIN_USER, config.web_password)) {
      return request->requestAuthentication("Akita Switch Configuration"); // Realm matches POST
    }
    // --- End Authentication ---

    String body = "<div class='card'><form method='post' action='/config'>";
    body += "<h2>WiFi Settings</h2>";
    body += "<label for='ssid'>WiFi Network Name (SSID):</label><input type='text' name='ssid' id='ssid' value='" + String(config.wifi_ssid) + "' maxlength='32' placeholder='Your WiFi Name'><br>";
    body += "<label for='pass'>WiFi Password:</label><input type='password' name='pass' id='pass' value='" + String(config.wifi_password) + "' maxlength='64' placeholder='Your WiFi Password'><br>";
    body += "<h2>Device Settings</h2>";
    body += "<label for='i2c'>I2C Slave Address (Decimal):</label><input type='number' name='i2c' id='i2c' min='1' max='127' value='" + String(config.i2c_address) + "'><br>";
    body += "<label for='host'>OTA Hostname:</label><input type='text' name='host' id='host' value='" + String(config.ota_hostname) + "' maxlength='32' placeholder='e.g., akita-switch-office'><br>";
    body += "<label for='webpass'>Web Interface Password:</label><input type='password' name='webpass' id='webpass' value='" + String(config.web_password) + "' maxlength='32' placeholder='New admin password'><br>";
    // Add field for OTA password if needed
    body += "<button type='submit' class='button save'>Save Configuration & Reboot</button>"; body += "</form></div>";
    body += "<div class='card'><h2>System</h2><form method='post' action='/reboot'><button type='submit' class='button reboot'>Reboot Device Now</button></form></div>";
    if (request->hasParam("msg")) { body += "<div class='msg success'>" + request->getParam("msg")->value() + "</div>"; }
    else if (request->hasParam("error")) { body += "<div class='msg error'>" + request->getParam("error")->value() + "</div>"; }
    request->send(200, "text/html", buildHtml("Configuration", body));
  });

  // Configuration Page (POST - Save) - Add Authentication
  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request){
    // --- Add Basic Authentication ---
    if(!request->authenticate(WEB_ADMIN_USER, config.web_password)) {
      return request->requestAuthentication("Akita Switch Configuration"); // Realm matches GET
    }
    // --- End Authentication ---

    bool configChanged = false;
    int params = request->params();
    for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            String pname = p->name();
            String pvalue = p->value();
            if (pname == "ssid" && pvalue != config.wifi_ssid) { strlcpy(config.wifi_ssid, pvalue.c_str(), sizeof(config.wifi_ssid)); configChanged = true; }
            if (pname == "pass" && pvalue != config.wifi_password) { strlcpy(config.wifi_password, pvalue.c_str(), sizeof(config.wifi_password)); configChanged = true; }
            if (pname == "host" && pvalue != config.ota_hostname) { strlcpy(config.ota_hostname, pvalue.c_str(), sizeof(config.ota_hostname)); configChanged = true; }
            if (pname == "webpass" && pvalue != config.web_password && pvalue.length() > 0) { strlcpy(config.web_password, pvalue.c_str(), sizeof(config.web_password)); configChanged = true; } // Only update if not empty
            if (pname == "i2c") {
                int i2c_val = pvalue.toInt();
                if (i2c_val >= 1 && i2c_val <= 127 && (uint8_t)i2c_val != config.i2c_address) { config.i2c_address = (uint8_t)i2c_val; configChanged = true; }
                else if (i2c_val < 1 || i2c_val > 127) { Serial.println("WARN: Invalid I2C address received."); }
            }
        }
    }

    String redirectMsg = "";
    if (configChanged) {
        saveConfiguration();
        redirectMsg = "/config?msg=Configuration+Saved.+Rebooting...";
    } else {
        // Don't reboot if nothing changed? Or always reboot? Let's always reboot for simplicity after save attempt.
        redirectMsg = "/config?msg=Configuration+Saved+(No+Changes).+Rebooting...";
    }

    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Redirecting");
    response->addHeader("Location", redirectMsg);
    request->send(response);

    delay(1000); // Short delay to allow response sending
    ESP.restart();
  });

  // Reboot Handler (POST) - Add Authentication
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
    // --- Add Basic Authentication ---
    if(!request->authenticate(WEB_ADMIN_USER, config.web_password)) {
      return request->requestAuthentication("Akita Switch Configuration");
    }
    // --- End Authentication ---
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Rebooting device...");
    request->send(response);
    delay(1000);
    ESP.restart();
  });

  // Handle Not Found
  server.onNotFound([](AsyncWebServerRequest *request){ request->send(404, "text/plain", "Not found"); });

  // Start server
  server.begin();
  Serial.println("Web server started.");
}


// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- Akita Switch Sensor Module Booting (v4) ---");
  Serial.println("Copyright (C) 2025 Akita Engineering"); Serial.println("License: GPLv3");

#ifdef ENABLE_DEEP_SLEEP
  // Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Decide if this is a normal boot or a wake from deep sleep; skip full init on wake to save power.
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
      Serial.println("Normal boot (power-on/reset). Performing full initialization.");
      skipFullSetupOnWake = false;
  } else {
      Serial.printf("Woke from deep sleep (cause=%d). Performing reduced initialization to save power.\n", wakeCause);
      skipFullSetupOnWake = true;
  }
#endif

  loadConfiguration(); // Load WiFi, I2C, Hostname, Web Password

  // Initialize I2C Slave *using loaded address*
  Serial.print("Initializing I2C slave at address 0x"); Serial.println(config.i2c_address, HEX);
  Wire.begin(config.i2c_address); // Use configured address
  Wire.setBufferSize(MAX_I2C_BUFFER);
  Wire.onReceive(receiveI2CCommand);
  Wire.onRequest(requestI2CData);
  i2cSendBuffer[0] = '\0';

  // Initialize Terminal Block Pins
  Serial.println("Initializing Terminal Block pins.");
  pinMode(TERMINAL_BLOCK_1_PIN, OUTPUT); pinMode(TERMINAL_BLOCK_2_PIN, OUTPUT);
  digitalWrite(TERMINAL_BLOCK_1_PIN, LOW); digitalWrite(TERMINAL_BLOCK_2_PIN, LOW);

  // Initialize Sensor Pins & Objects
  Serial.println("Initializing Sensor pins/objects.");
  pinMode(WATER_LEVEL_PIN, INPUT); // Analog pin needs no special init beyond pinMode

  // --- Add REAL Sensor Initialization Here ---
  // Example: dht.begin(); // Initialize DHT sensor object
  // Example: if (!bme.begin(BME_I2C_ADDRESS)) { Serial.println("Could not find a valid BME280 sensor, check wiring/address!"); } // Initialize BME280
  // Example: if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) { Serial.println("Error initializing BH1750"); } // Initialize BH1750
  // -----------------------------------------

  if (!skipFullSetupOnWake) {
      setupWiFi(); // Tries STA then AP, enables OTA if STA connects
      setupWebServer(); // Runs in both modes
  } else {
      Serial.println("Skipping WiFi and WebServer initialization after deep-sleep wake to conserve power.");
      // Ensure state variables reflect limited initialization
      wifiConnected = false;
      apMode = false;
  }

  readSensors(); // Initial sensor read

  #ifdef ENABLE_DEEP_SLEEP
    lastActivityMillis = millis();
  #endif

  Serial.println("--- Setup Complete. Akita Sensor Ready. ---");

#ifdef ENABLE_DEEP_SLEEP
   // Optional: Go directly to sleep after setup if condition met
   // if (shouldGoToSleepImmediately()) { goToDeepSleep(); }
#endif
}

// --- Arduino Loop ---
void loop() {
#ifdef ENABLE_DEEP_SLEEP
  // --- Deep Sleep Logic Example ---
  Serial.println("Performing loop tasks (deep-sleep mode)...");
  if (wifiConnected) { ArduinoOTA.handle(); }
  readSensors(); // Refresh sensors before evaluating sleep conditions

  // If master recently requested I2C data, give a short grace period to ensure it has read the latest values
  if (lastI2CRequestMillis && (millis() - lastI2CRequestMillis) < 2000) {
      Serial.println("Master recently requested I2C data — delaying sleep briefly to ensure read completes.");
      delay(250); // small grace delay
  }

  // Decide whether to sleep: inactive for configured threshold OR forced by other conditions (battery, etc.)
  if ((millis() - lastActivityMillis) >= INACTIVITY_BEFORE_SLEEP_MS) {
      Serial.println("Inactivity threshold reached — preparing to enter deep sleep.");
      Serial.printf("Entering deep sleep for %d seconds.\n", DEEP_SLEEP_SECONDS);
      Serial.flush();
      goToDeepSleep();
  } else {
      Serial.println("Activity detected — staying awake for now.");
      delay(1000); // Short delay to conserve CPU while waiting for inactivity
  }

  // --- End Deep Sleep Logic Example ---
#else
  // --- Normal Operation (No Deep Sleep) ---
  if (wifiConnected) {
    ArduinoOTA.handle(); // Handle OTA updates if connected
  }
  // Add any other periodic, non-blocking tasks here.
  // Example: Check sensor readings more frequently if needed for local alerts.
  delay(10); // Yield to system tasks
#endif
}

// --- Power Management ---
/**
 * @brief Placeholder function to enter deep sleep. Configure wake sources before calling.
 */
void goToDeepSleep() {
#ifdef ENABLE_DEEP_SLEEP
  // Example: Wake up using timer
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * 1000000ULL); // Time in microseconds (use ULL for large numbers)
  Serial.println("Configured Timer as wake up source");

  // Example: Wake up on GPIO pin (e.g., connected to I2C interrupt or external button)
  // const int wakePin = GPIO_NUM_XX; // Choose appropriate GPIO
  // esp_sleep_enable_ext0_wakeup((gpio_num_t)wakePin, 1); // 1 = High level trigger
  // Serial.printf("Configured GPIO %d as wake up source\n", wakePin);

  Serial.println("Entering Deep Sleep...");
  esp_deep_sleep_start(); // Device will restart from setup() upon wake-up

  // Code below here will not execute after deep sleep starts
#else
  Serial.println("Deep sleep function called, but ENABLE_DEEP_SLEEP is not defined.");
#endif
}
