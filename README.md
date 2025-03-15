# Akita Switch for Meshtastic

The Akita Switch is a versatile sensor/actuator system designed to extend the capabilities of Meshtastic devices, particularly those based on the ESP32 platform like the Heltec LoRa 32 v3. It enables remote monitoring and control of sensors and terminal blocks through a web interface and Meshtastic network.

## Features

* **Sensor Data Collection:** Simulates or reads real-time sensor data (temperature, humidity, pressure, lux, analog water level).
* **Terminal Block Control:** Controls terminal blocks (digital outputs) via web interface and Meshtastic commands.
* **Web-Based Configuration:** Provides a SoftAP and web server for easy setup and control.
* **Meshtastic Integration:** Transmits sensor data and receives commands over the Meshtastic network.
* **JSON Message Format:** Uses JSON for structured data exchange, including sensor data, commands, and acknowledgments.
* **Message Acknowledgments:** Ensures reliable command execution through acknowledgment messages.
* **Robust Error Handling:** Implements error handling for I2C communication and JSON parsing.
* **Sequence Numbers:** Prevents duplicate command execution using sequence numbers.
* **Timestamping:** Includes timestamps in messages for data tracking.
* **Real Sensor Simulation:** Includes simulated sensor readings for easy testing.

## Hardware Requirements

* ESP32-based development board (e.g., Heltec LoRa 32 v3)
* Sensor ESP32 module
* Sensors (temperature, humidity, pressure, lux, analog water level)
* Terminal blocks (or relays)
* I2C wires (SDA, SCL, GND, VCC)

## Software Requirements

* Arduino IDE with ArduinoJson and ESPAsyncWebServer libraries
* Python 3 with `meshtastic` and `smbus2` libraries (`pip install meshtastic smbus2`)

## Installation and Usage

1.  **Hardware Setup:**
    * Connect the sensor ESP32 module to the Meshtastic device (Heltec v3) via I2C (SDA, SCL, GND, VCC).
    * Connect sensors to the sensor ESP32 module.
    * Connect terminal blocks to the sensor ESP32 module.

2.  **Software Setup:**
    * Install the Arduino IDE and ArduinoJson, and ESPAsyncWebServer libraries.
    * Install the `meshtastic` and `smbus2` Python libraries.
    * Upload `Akita_Sensor.ino` to the sensor ESP32 module using the Arduino IDE.
    * Run `akita_meshtastic.py` on the computer connected to the Meshtastic device.

3.  **Configuration:**
    * Connect to the "AkitaSwitchSetup" WiFi network.
    * Open a web browser and navigate to `192.168.4.1`.
    * Control the terminal blocks through the web interface.

4.  **Testing:**
    * Observe the sensor readings on the web page.
    * Use the Meshtastic CLI or app to send JSON commands to control terminal blocks.
    * Observe the sensor data being transmitted over the Meshtastic network.

## Code Structure

* **`Akita_Sensor.ino`:** Arduino code for the sensor ESP32 module (SoftAP, web server, sensor readings, terminal block control).
* **`akita_meshtastic.py`:** Python code for the Meshtastic device (Heltec v3) (Meshtastic communication, I2C commands).

## JSON Message Format

* **Sensor Data:**

    ```json
    {
      "type": "sensor_data",
      "timestamp": 1678886400,
      "sequence": 123,
      "temp": 25.5,
      "humidity": 60.2,
      "pressure": 1013.0,
      "lux": 100,
      "waterLevel": 512
    }
    ```

* **Command:**

    ```json
    {
      "type": "command",
      "timestamp": 1678886460,
      "sequence": 124,
      "terminal": 1,
      "action": "on"
    }
    ```

* **Acknowledgment:**

    ```json
    {
      "type": "acknowledgment",
      "timestamp": 1678886465,
      "sequence": 124,
      "status": "success"
    }
    ```

## Enhancements

* **Real Sensor Integration:** Replace simulated sensor readings with actual sensor code.
* **Advanced Web Interface:** Implement a more feature-rich web interface for sensor data visualization and configuration.
* **Data Logging:** Log sensor data to a file or cloud service.
* **Power Management:** Implement low-power modes for battery conservation.
* **Over-the-Air (OTA) Updates:** Enable OTA updates for the sensor ESP32 firmware.
* **Security:** Implement encryption and authentication for secure communication.
* **Specific error codes:** Add more specific error codes to the acknowledgement messages.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bug reports or feature requests.

