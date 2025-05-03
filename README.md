# Akita Switch for Meshtastic

**Organization:** Akita Engineering
**Website:** [www.akitaengineering.com](https://www.akitaengineering.com)
**Contact:** info@akitaengineering.com
**License:** GNU General Public License v3.0 (GPLv3)

The Akita Switch is a versatile sensor/actuator system designed to extend the capabilities of Meshtastic devices, particularly those based on the ESP32 platform. It enables remote monitoring of sensors and control of terminal blocks through a local web interface and the Meshtastic network.

**New in v4:** Added placeholders/examples for real sensor integration (DHT, BME280, BH1750), basic web authentication for config page, OTA password option, improved web styling, placeholders for power management (firmware), and placeholders/arguments for MQTT publishing (host script).

This project consists of two main parts:
1.  **Firmware (`Akita_Sensor.ino`):** Runs on an ESP32. Acts as I2C slave, reads sensors, controls outputs, hosts SoftAP/Web Server (with basic auth), connects to WiFi for OTA (with optional password), handles config persistence (SPIFFS).
2.  **Host Script (`akita_meshtastic.py`):** Runs on a host computer (e.g., RPi). Acts as I2C master, relays Meshtastic commands, sends sensor data to Meshtastic, logs sensor data locally, includes placeholders/config for MQTT publishing.

## Features

* **Sensor Data Collection:** Reads real-time or simulated sensor data. *Includes library includes and commented-out code examples for DHTxx, BME280, BH1750, and Analog sensors.*
* **Terminal Block Control:** Controls digital outputs via local web interface and Meshtastic commands.
* **Configuration Persistence:** Stores WiFi credentials, I2C address, OTA hostname, and Web Admin password in SPIFFS on the ESP32.
* **Web-Based Configuration & Status:**
    * Provides SoftAP (`AkitaSwitchSetup-XXXX`) for initial setup.
    * Serves web pages for status/control and configuration (`/config`).
    * **Basic HTTP Authentication** added to `/config` page (default user: `admin`, pass: `akita` - changeable via config file/web).
* **Over-the-Air (OTA) Updates:** Supports firmware updates via ArduinoOTA when connected to WiFi. **Optional OTA password** can be set in firmware.
* **Meshtastic Integration:** Transmits sensor data and receives commands over Meshtastic using a configurable private PortNum.
* **Local Data Logging:** Host script logs sensor data (JSON Lines) to a rotating local file (configurable path, size, backups).
* **MQTT Publishing (Placeholder):** Host script includes configuration arguments and code placeholders for publishing sensor data to an MQTT broker. Requires `paho-mqtt` library (`pip install paho-mqtt`).
* **Reliable I2C Communication:** Implements sequence numbers and acknowledgments.
* **Robust Error Handling:** Includes error handling for I2C, JSON, WiFi, File I/O, MQTT connection (basic).
* **Timestamping:** Uses ESP32 `millis()` and host timestamps.
* **Power Management (Placeholder):** Firmware includes commented-out sections and `#ifdef` blocks showing where deep sleep logic could be integrated.

## Hardware Requirements

* ESP32 board, Host computer, Meshtastic Node, Optional Sensors/Relays, I2C Wiring (with pull-ups).

## Software Requirements

* **Sensor Module:**
    * Arduino IDE/PlatformIO, ESP32 Core.
    * Libraries: `Wire`, `WiFi`, `ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson` (v6+), `SPIFFS`, `ArduinoOTA`.
    * **Optional Sensor Libraries:** `DHT sensor library`, `Adafruit Unified Sensor`, `Adafruit BME280`, `Adafruit BMP280`, `BH1750` by Christopher Laws (install needed ones via Library Manager).
* **Meshtastic Bridge Host:**
    * Python 3 (>= 3.7).
    * Libraries: `meshtastic`, `pyserial`, `smbus2` (Linux), `paho-mqtt` (optional, for MQTT). Install via `pip install -r host_script/requirements.txt`.

## Installation and Setup

**1. Hardware Connections:** 

**2. Sensor Module Firmware (`Akita_Sensor.ino`):**
    * Open `firmware/Akita_Sensor/Akita_Sensor.ino`.
    * **Sensor Integration:**
        * Uncomment `#include` lines for the sensor libraries you are using.
        * Update sensor pin definitions (`#define ..._PIN`) or I2C addresses (`#define ..._ADDRESS`) if needed.
        * Uncomment and adapt the sensor object creation lines (e.g., `DHT dht(...)`).
        * In `setup()`, uncomment and adapt sensor initialization code (e.g., `dht.begin()`, `bme.begin(...)`).
        * In `readSensors()`, comment out the simulation block and uncomment/adapt the reading code for your sensors.
    * **Security:** Consider changing the default `WEB_ADMIN_PASS` define before first flashing, or change it via the web interface later. Consider uncommenting and setting an `ArduinoOTA.setPassword(...)` in `setupOTA()`.
    * Install required libraries (Core + Sensor libs) via Arduino Library Manager.
    * Select board, upload sketch. Monitor Serial (115200 baud).

**3. Initial Configuration (Web Interface):**
    * Connect to SoftAP (`AkitaSwitchSetup-XXXX`, pass: `password`), navigate to `http://192.168.4.1`.
    * Go to `/config`. You will be prompted for authentication (default: `admin` / `akita`).
    * Enter WiFi details, I2C Address, Hostname, and optionally change the Web Admin Password.
    * Save. Device reboots and should connect to your WiFi.

**4. Meshtastic Bridge Host Script (`akita_meshtastic.py`):**
    * Navigate to `host_script`.
    * Install/update dependencies: `pip install -r requirements.txt`.
    * (Linux) Ensure user is in `i2c` and `dialout` groups.
    * Run script (use `--help` for all options, including MQTT):
        ```bash
        # Example without MQTT:
        python akita_meshtastic.py --port /dev/ttyUSB0 --slave-addr 0x04 --log-level INFO

        # Example with MQTT enabled:
        python akita_meshtastic.py --slave-addr 0x04 --mqtt-broker 192.168.1.100 --mqtt-topic akita/office/sensors --mqtt-user myuser --mqtt-pass mypass
        ```

## Usage

* **Local Web Interface:** Access via ESP32's IP. Use `/` for status/control, `/config` for settings (requires login).
* **Meshtastic Network:** Send commands, receive sensor data.
* **OTA Updates:** Use Arduino IDE OTA (Tools -> Port -> YourHostname). Requires OTA password if set in firmware.
* **Sensor Data Logging:** Data appended to file specified by `--sensor-log` on host. Rotates automatically.
* **MQTT Publishing:** If `--mqtt-broker` is set, sensor data (JSON) will be published to the specified topic.

## Notes on Further Enhancements

* **Advanced Web Interface:** Use Chart.js (requires adding JS/HTML to firmware) or build a separate host dashboard (e.g., Node-RED, Grafana reading from MQTT or the log file).
* **Cloud Logging (MQTT/InfluxDB):** The host script now has MQTT arguments and placeholders. Complete the `publish_mqtt` logic if needed, handle connection errors robustly. For InfluxDB, add the `influxdb-client` library and similar publishing logic.
* **Power Management:** The firmware has `#ifdef ENABLE_DEEP_SLEEP` blocks. To implement:
    * Uncomment the `#define`.
    * Modify `loop()` to call `goToDeepSleep()` based on your criteria (e.g., timer, inactivity).
    * In `goToDeepSleep()`, configure appropriate wake-up sources (`esp_sleep_enable_timer_wakeup`, `esp_sleep_enable_extX_wakeup`).
    * Ensure critical state is saved/restored if needed (e.g., using RTC memory via `RTC_DATA_ATTR`). Deep sleep significantly alters program flow.
* **Security:** Use a strong, unique Meshtastic channel PSK. Change default web/OTA passwords. Consider HTTPS for the web server if exposed (requires more complex setup).
* **I2C Robustness (Chunking):** If JSON often exceeds ~100 bytes (approaching `MAX_I2C_BUFFER`) or >32 bytes for commands (due to `smbus` write limits), implement chunking. This involves defining a start/middle/end chunk format, adding sequence numbers to chunks, and modifying both firmware and host script I2C handlers to split/reassemble messages.

## Contributing

Contributions welcome! See `CONTRIBUTING.md`.

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

