#!/usr/bin/env python3
"""
Akita Switch for Meshtastic - Host Bridge Script

Copyright (C) 2025 Akita Engineering <info@akitaengineering.com>
Website: https://www.akitaengineering.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.

--- Description ---
v4: Implemented MQTT publishing logic.
Connects to a Meshtastic node via Serial/USB and acts as an I2C master
to communicate with the Akita Sensor Module (ESP32 firmware).

- Relays commands received over Meshtastic to the Sensor Module via I2C.
- Periodically reads sensor data from the Sensor Module via I2C.
- Broadcasts sensor data over the Meshtastic network.
- Logs received sensor data to a local file (JSON Lines format).
- **Publishes sensor data to an MQTT broker if configured.**
- Handles I2C communication, JSON parsing, command acknowledgments.
- Uses smbus2 for I2C communication on Linux systems.
"""

import meshtastic
import meshtastic.serial_interface
import meshtastic.portnums
import time
import json
import argparse
import sys
import logging
import os
from logging.handlers import RotatingFileHandler
from smbus2 import SMBus
import paho.mqtt.client as paho # Import MQTT client library
import ssl # For MQTT TLS support if needed

# --- Default Configuration ---
DEFAULT_SLAVE_ADDRESS = 0x04
DEFAULT_I2C_BUS = 1
DEFAULT_MESH_PORTNUM = meshtastic.portnums.PortNum.PRIVATE_APP + 3 # Example port (27)
DEFAULT_SENSOR_READ_INTERVAL_S = 60.0
DEFAULT_POLL_INTERVAL_S = 0.2
DEFAULT_COMMAND_TIMEOUT_S = 5.0
DEFAULT_MAX_I2C_READ_LEN = 128
DEFAULT_LOG_LEVEL = "INFO"
DEFAULT_SENSOR_LOG_FILE = "sensor_data.log" # Default file for sensor readings
DEFAULT_SENSOR_LOG_MAX_BYTES = 5 * 1024 * 1024 # 5 MB max log size
DEFAULT_SENSOR_LOG_BACKUP_COUNT = 3 # Keep 3 backup log files
# MQTT Defaults (Disabled by default, set broker to enable)
DEFAULT_MQTT_BROKER = None # Set to IP or hostname to enable MQTT
DEFAULT_MQTT_PORT = 1883
DEFAULT_MQTT_TOPIC = "akita/sensor/data"
DEFAULT_MQTT_USER = None
DEFAULT_MQTT_PASS = None
DEFAULT_MQTT_CLIENT_ID = "akita_meshtastic_bridge"
DEFAULT_MQTT_QOS = 0
DEFAULT_MQTT_RETAIN = False
DEFAULT_MQTT_TLS = False # Set to True to enable TLS
DEFAULT_MQTT_TLS_INSECURE = False # Set to True to skip hostname verification (use with caution)
DEFAULT_MQTT_CA_CERTS = None # Path to CA certificate file for TLS

# --- Globals ---
i2c_bus = None
meshtastic_interface = None
last_sensor_send_time = 0
args = None
sensor_logger = None # Dedicated logger for sensor data
mqtt_client = None
mqtt_connected = False
mqtt_connection_attempted = False

# Setup general application logging
log = logging.getLogger(__name__) # Logger for general script operations
log.setLevel(DEFAULT_LOG_LEVEL.upper()) # Set default level
# Basic console handler
console_handler = logging.StreamHandler()
console_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
console_handler.setFormatter(console_formatter)
# Prevent adding handler multiple times if script is reloaded (e.g., in some dev environments)
if not log.handlers:
    log.addHandler(console_handler)


# --- Sensor Data Logging Setup ---
def setup_sensor_logging(log_file_path, max_bytes, backup_count):
    """Configures a dedicated logger for sensor data using RotatingFileHandler."""
    global sensor_logger
    if sensor_logger: # Avoid configuring multiple times
        return True
    try:
        sensor_logger = logging.getLogger('SensorData') # Use a distinct name
        sensor_logger.setLevel(logging.INFO) # Log all sensor readings passed to it
        sensor_logger.propagate = False # Prevent sensor logs going to console via root

        # Use RotatingFileHandler to manage log file size
        log_dir = os.path.dirname(log_file_path)
        if log_dir and not os.path.exists(log_dir):
             os.makedirs(log_dir) # Create log directory if it doesn't exist

        file_handler = RotatingFileHandler(
            log_file_path,
            maxBytes=max_bytes,
            backupCount=backup_count
        )
        # Use a simple formatter for JSON Lines format (just the message)
        file_formatter = logging.Formatter('%(message)s')
        file_handler.setFormatter(file_formatter)

        sensor_logger.addHandler(file_handler)
        log.info(f"Sensor data logging configured to file: {log_file_path}")
        return True
    except Exception as e:
        log.error(f"Failed to configure sensor data logging to {log_file_path}: {e}")
        sensor_logger = None # Ensure it's None if setup fails
        return False

# --- I2C Functions ---
def setup_i2c(bus_num, slave_addr):
    """Initialize the I2C bus connection using smbus2."""
    global i2c_bus
    if i2c_bus: return True # Already initialized
    try:
        log.info(f"Initializing I2C bus {bus_num}...")
        i2c_bus = SMBus(bus_num, force=True) # Use force=True if device detection issues occur
        try:
             log.debug(f"Pinging I2C slave at 0x{slave_addr:02x}...")
             i2c_bus.read_i2c_block_data(slave_addr, 0, 1)
             log.info(f"Successfully communicated with I2C slave 0x{slave_addr:02x} on bus {bus_num}.")
        except OSError as read_err:
             log.warning(f"Initial I2C read check to slave 0x{slave_addr:02x} failed: {read_err}. Continuing...")
        return True
    except FileNotFoundError: log.error(f"ERROR: I2C bus {bus_num} not found."); return False
    except PermissionError: log.error(f"ERROR: Permission denied accessing I2C bus {bus_num}. Add user to 'i2c' group."); return False
    except OSError as e: log.error(f"ERROR: Failed to initialize I2C bus {bus_num} or talk to slave 0x{slave_addr:02x}: {e}"); return False
    except Exception as e: log.error(f"ERROR: Unexpected error initializing I2C: {e}"); return False

def parse_i2c_data(raw_data_list):
    """Safely decode list of bytes from I2C read and parse JSON."""
    if not raw_data_list: log.debug("parse_i2c_data received empty list."); return None
    try:
        raw_bytes = bytes(raw_data_list)
        null_index = raw_bytes.find(b'\0')
        if null_index != -1: json_string = raw_bytes[:null_index].decode('utf-8', errors='ignore').strip()
        else: json_string = raw_bytes.decode('utf-8', errors='ignore').strip()
        log.debug(f"Decoded I2C JSON string: '{json_string}'")
        if json_string: return json.loads(json_string)
        else: log.debug("Empty JSON string received from I2C."); return None
    except UnicodeDecodeError: log.warning(f"I2C Read Decode Error. Raw bytes: {raw_bytes}"); return None
    except json.JSONDecodeError as e: log.warning(f"I2C Invalid JSON received: '{json_string}'. Error: {e}"); return None
    except ValueError as e: log.error(f"Error converting I2C data list to bytes: {e}. Data: {raw_data_list}"); return None
    except Exception as e: log.error(f"Unexpected error parsing I2C data: {e}"); return None

def read_i2c_data(slave_addr, max_len):
    """Reads a block of data from I2C slave, handling potential errors."""
    if not i2c_bus: log.error("I2C bus not initialized."); return None
    try:
        log.debug(f"Reading up to {max_len} bytes from I2C slave 0x{slave_addr:02x}...")
        data_list = i2c_bus.read_i2c_block_data(slave_addr, 0, max_len)
        log.debug(f"Read I2C raw byte list: {data_list}")
        return parse_i2c_data(data_list)
    except OSError as e: log.debug(f"I2C read error from slave 0x{slave_addr:02x}: {e}"); return None # Common if slave busy
    except Exception as e: log.error(f"Unexpected error reading I2C: {e}"); return None

def send_i2c_command(command, slave_addr, timeout, poll_interval, max_read_len):
    """Sends a command dict as JSON via I2C and waits for a specific acknowledgment JSON."""
    if not i2c_bus: log.error("I2C bus not initialized."); return False
    sequence = command.get("sequence")
    if sequence is None:
        log.error("Command missing 'sequence'.")
        return False
    try:
        json_command = json.dumps(command, separators=(',', ':')); command_bytes_list = list(bytes(json_command, 'utf-8'))
        if len(command_bytes_list) > 32: log.warning(f"Command JSON > 32 bytes ({len(command_bytes_list)} bytes). SMBus write may truncate!")
        log.info(f"Sending I2C command (Seq: {sequence}): {json_command}")
        i2c_bus.write_i2c_block_data(slave_addr, 0, command_bytes_list)
        start_time = time.time(); ack_received = False
        while time.time() - start_time < timeout:
            log.debug(f"Waiting for ACK for sequence {sequence}...")
            ack_data = read_i2c_data(slave_addr, max_read_len)
            if ack_data:
                 log.debug(f"Received potential ACK data: {ack_data}")
                 if (isinstance(ack_data, dict) and ack_data.get("type") == "acknowledgment" and ack_data.get("sequence") == sequence):
                    status = ack_data.get("status"); error_code = ack_data.get("error_code")
                    if status == "success":
                        if error_code: log.info(f"Command (Seq: {sequence}) acked '{status}' with info: '{error_code}'")
                        else: log.info(f"Command (Seq: {sequence}) acked successfully.")
                        ack_received = True; break
                    else: log.error(f"Command (Seq: {sequence}) failed. Slave Status: '{status}', Code: '{error_code or 'N/A'}'"); ack_received = False; break
                 elif ack_data.get("type") == "sensor_data": log.debug("Received sensor data while waiting for ACK.")
                 else: log.warning(f"Received unexpected type '{ack_data.get('type')}' while waiting for ACK (Seq: {sequence}).")
            time.sleep(poll_interval)
        if not ack_received:
            if time.time() - start_time >= timeout: log.error(f"Command (Seq: {sequence}) timed out after {timeout:.1f}s waiting for ACK.")
            return False
        else: return True
    except OSError as e: log.error(f"I2C error during send/ack (Seq: {sequence}): {e}"); return False
    except Exception as e: log.error(f"Unexpected error sending/receiving ACK (Seq: {sequence}): {e}"); return False

# --- Meshtastic Functions ---
def setup_meshtastic(device_port=None):
    """Initialize the Meshtastic serial interface and register callback."""
    global meshtastic_interface
    if meshtastic_interface: return True # Already initialized
    try:
        log.info("Connecting to Meshtastic device...")
        if device_port: log.info(f"Using specified port: {device_port}"); meshtastic_interface = meshtastic.serial_interface.SerialInterface(devPath=device_port)
        else: log.info("Attempting auto-detect..."); meshtastic_interface = meshtastic.serial_interface.SerialInterface()
        log.info("Waiting for node info...")
        time.sleep(3)
        if not meshtastic_interface or not meshtastic_interface.myInfo or not meshtastic_interface.nodeInfo:
            log.error("Failed to get node info.")
            if meshtastic_interface:
                meshtastic_interface.close()
            return False
        my_node_num = meshtastic_interface.myInfo.my_node_num
        my_node_id = meshtastic_interface.myInfo.node_id
        my_user_id = meshtastic_interface.myInfo.user_id
        log.info(f"Connected to Meshtastic node: User='{my_user_id}', ID={my_node_id}, Num={my_node_num}")
        log.info(f"Meshtastic lib v{meshtastic.__version__}, Device FW: {meshtastic_interface.nodeInfo.firmware_version}")
        meshtastic_interface.add_receive_callback(on_mesh_receive)
        log.info("Meshtastic receive callback registered.")
        return True
    except meshtastic.MeshtasticError as e:
        log.error(f"Meshtastic connection error: {e}")
        if not device_port:
            log.error("Try specifying --port.")
        if meshtastic_interface:
            meshtastic_interface.close()
        return False
    except Exception as e:
        log.error(f"Unexpected error setting up Meshtastic: {e}")
        if meshtastic_interface:
            meshtastic_interface.close()
        return False

def on_mesh_receive(packet, interface): # pylint: disable=unused-argument
    """Callback function executed when a packet is received from the Meshtastic network."""
    global args
    try:
        if not packet or 'decoded' not in packet or 'portnum' not in packet['decoded']: log.debug("Ignoring invalid packet."); return
        portnum = packet['decoded']['portnum']; payload_bytes = packet['decoded'].get('payload')
        if portnum == args.mesh_portnum and payload_bytes:
            sender_node_id = packet.get('fromId', 'Unknown')
            log.info(f"Received Meshtastic message on PortNum {portnum} from {sender_node_id}")
            try:
                payload_text = payload_bytes.decode('utf-8')
                log.debug(f"Decoded Payload: {payload_text}")
                command = json.loads(payload_text)
                if not isinstance(command, dict): log.warning(f"Received non-dict JSON from {sender_node_id}."); return
                cmd_type = command.get("type"); sequence = command.get("sequence")
                if cmd_type == "command" and sequence is not None:
                    log.info(f"Received valid command from {sender_node_id} (Seq: {sequence})")
                    if "timestamp" not in command: command["timestamp"] = int(time.time()); log.debug("Added host timestamp.")
                    if "terminal" in command and "action" in command:
                         send_i2c_command(command, args.slave_addr, args.timeout, args.poll_interval, args.max_read_len)
                    else: log.warning(f"Command from {sender_node_id} (Seq: {sequence}) missing terminal/action.")
                else: log.warning(f"Msg from {sender_node_id} not valid command (type='{cmd_type}', seq={sequence}).")
            except UnicodeDecodeError: log.warning(f"Received non-UTF8 payload from {sender_node_id}.")
            except json.JSONDecodeError as e: log.warning(f"Invalid JSON from {sender_node_id}: '{payload_text}'. Error: {e}")
            except Exception as e: log.error(f"Error processing command from {sender_node_id}: {e}", exc_info=True)
        # else: pass # Ignore packets not matching criteria
    except Exception as e: log.error(f"Critical error in on_mesh_receive: {e}", exc_info=True)

# --- MQTT Functions ---
def on_mqtt_connect(client, userdata, flags, rc, properties=None): # Add properties for paho v2+ compatibility
    """Callback for when MQTT client connects."""
    global mqtt_connected
    if rc == 0:
        log.info("MQTT Connected successfully.")
        mqtt_connected = True
    else:
        log.error(f"MQTT Connection failed with code {rc}. Check broker, port, credentials, TLS settings.")
        mqtt_connected = False

def on_mqtt_disconnect(client, userdata, rc, properties=None): # Add properties for paho v2+ compatibility
    """Callback for when MQTT client disconnects."""
    global mqtt_connected
    log.warning(f"MQTT Disconnected with result code {rc}.")
    mqtt_connected = False
    # Optional: Add reconnection logic here if needed, e.g., using client.reconnect() in main loop

def on_mqtt_publish(client, userdata, mid):
    """Callback for when a message is published (confirms PUBACK for QoS>0)."""
    log.debug(f"MQTT message published (mid={mid}).")

def setup_mqtt(args):
    """Initializes and connects the MQTT client if broker is specified."""
    global mqtt_client, mqtt_connected, mqtt_connection_attempted
    if not args.mqtt_broker:
        log.info("MQTT broker not specified, MQTT publishing disabled.")
        return False
    if mqtt_client: # Avoid re-initializing if already setup
        return True

    log.info(f"Setting up MQTT client for broker {args.mqtt_broker}:{args.mqtt_port}...")
    try:
        # Use MQTTv5 by default if available, fallback might be needed for older brokers
        mqtt_client = paho.Client(client_id=args.mqtt_client_id, protocol=paho.MQTTv5)
        mqtt_client.on_connect = on_mqtt_connect
        mqtt_client.on_disconnect = on_mqtt_disconnect
        mqtt_client.on_publish = on_mqtt_publish

        if args.mqtt_user and args.mqtt_pass:
            log.info(f"Using MQTT username: {args.mqtt_user}")
            mqtt_client.username_pw_set(args.mqtt_user, args.mqtt_pass)
        elif args.mqtt_user:
             log.warning("MQTT username provided without password.")
             mqtt_client.username_pw_set(args.mqtt_user)

        # Configure TLS if enabled
        if args.mqtt_tls:
            log.info("Enabling MQTT TLS...")
            # Choose TLS version - default is usually fine
            tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            # Load default system CAs
            tls_context.load_default_certs()
            if args.mqtt_ca_certs:
                 if os.path.exists(args.mqtt_ca_certs):
                     log.info(f"Loading custom CA certs from: {args.mqtt_ca_certs}")
                     tls_context.load_verify_locations(cafile=args.mqtt_ca_certs)
                 else:
                      log.error(f"MQTT CA cert file not found: {args.mqtt_ca_certs}")
                      # Decide whether to proceed without custom CAs or fail
                      # return False # Option: Fail if custom CA not found

            if args.mqtt_tls_insecure:
                 log.warning("MQTT TLS insecure mode enabled - hostname verification disabled!")
                 tls_context.check_hostname = False
                 tls_context.verify_mode = ssl.CERT_NONE
            else:
                 tls_context.verify_mode = ssl.CERT_REQUIRED # Default

            mqtt_client.tls_set_context(tls_context)
            # Deprecated tls_set: mqtt_client.tls_set(ca_certs=args.mqtt_ca_certs, cert_reqs=ssl.CERT_REQUIRED if not args.mqtt_tls_insecure else ssl.CERT_NONE)
            # Deprecated tls_insecure_set: mqtt_client.tls_insecure_set(args.mqtt_tls_insecure)

        # Start connection attempt (non-blocking)
        mqtt_client.connect_async(args.mqtt_broker, args.mqtt_port, 60)
        mqtt_client.loop_start() # Start background network loop
        mqtt_connection_attempted = True
        log.info("MQTT connection initiated (async). Waiting for connection...")
        # Connection status will be updated by on_mqtt_connect callback
        return True # Setup initiated

    except Exception as e:
        log.error(f"Failed to setup MQTT client: {e}", exc_info=True)
        mqtt_client = None
        return False

def publish_mqtt(topic, payload_str, qos, retain):
    """Publishes a string payload to the configured MQTT topic."""
    global mqtt_client, mqtt_connected
    if not mqtt_client:
        log.debug("MQTT client not initialized, cannot publish.")
        return False

    if not mqtt_connected:
        log.warning("MQTT not connected, cannot publish.")
        # Optional: Could queue messages here for later sending upon reconnect
        return False

    try:
        log.debug(f"Publishing to MQTT topic '{topic}' (QoS={qos}, Retain={retain}): {payload_str}")
        # Publish with specified QoS and Retain flag
        msg_info = mqtt_client.publish(topic, payload_str, qos=qos, retain=retain)

        if qos > 0:
            try:
                # Wait briefly for publish confirmation for QoS > 0
                msg_info.wait_for_publish(timeout=2.0)
                if msg_info.is_published():
                    log.debug(f"MQTT message (mid={msg_info.mid}) published successfully.")
                    return True
                else:
                    log.warning(f"MQTT publish (mid={msg_info.mid}) may not have completed within timeout.")
                    return False
            except RuntimeError as e:
                 log.warning(f"MQTT publish confirmation error (mid={msg_info.mid}): {e}")
                 return False
            except ValueError as e: # Can happen if loop isn't running
                 log.warning(f"MQTT publish confirmation error (mid={msg_info.mid}), loop might not be running: {e}")
                 return False
        else:
            # For QoS 0, publish is fire-and-forget, assume success if no immediate error
            log.debug("MQTT QoS 0 message sent.")
            return True

    except Exception as e:
        log.error(f"Error publishing to MQTT topic '{topic}': {e}")
        # Check for specific errors indicating disconnection
        if isinstance(e, (OSError, paho.WebsocketConnectionError, paho.MQTTException)):
             log.warning("MQTT connection likely lost during publish.")
             mqtt_connected = False # Update connection status
        return False


# --- Combined Data Handling ---
def process_and_distribute_sensor_data(sensor_data, mesh_interface, mesh_port_num, mqtt_topic, mqtt_qos, mqtt_retain):
    """Logs sensor data, sends over Meshtastic, and publishes via MQTT."""
    global last_sensor_send_time, sensor_logger # Access sensor logger & MQTT client

    if not isinstance(sensor_data, dict):
        log.warning("Invalid sensor data format received, cannot process."); return

    try:
        # Add a host timestamp for logging/MQTT
        host_timestamp = int(time.time())
        sensor_data['host_ts'] = host_timestamp
        sensor_json = json.dumps(sensor_data, separators=(',', ':')) # Compact JSON

        # 1. Log to local file
        if sensor_logger:
            try:
                sensor_logger.info(sensor_json)
                log.debug("Sensor data logged to file.")
            except Exception as log_e:
                log.error(f"Failed to write sensor data to log file: {log_e}")
        else:
            log.debug("Sensor data file logger not configured.") # Changed from warning

        # 2. Publish to MQTT (if enabled)
        if mqtt_client:
            publish_mqtt(mqtt_topic, sensor_json, mqtt_qos, mqtt_retain)
        # else: MQTT not configured

        # 3. Send over Meshtastic
        if mesh_interface:
            log.info(f"Publishing sensor data (Seq: {sensor_data.get('sequence', 'N/A')}) to Meshtastic PortNum {mesh_port_num}")
            log.debug(f"Sensor JSON: {sensor_json}")
            mesh_interface.sendText(sensor_json, destinationId='^all', channelIndex=0, portNum=mesh_port_num)
            last_sensor_send_time = time.time() # Update time only on successful send attempt
            log.debug("Sensor data sent successfully via Meshtastic.")
        else:
            log.warning("Meshtastic interface not available, cannot send sensor data.")

    except meshtastic.MeshtasticError as e: log.error(f"Meshtastic send error: {e}")
    except TypeError as e: log.error(f"Error serializing sensor data: {e}. Data: {sensor_data}")
    except Exception as e: log.error(f"Unexpected error processing/distributing sensor data: {e}", exc_info=True)


# --- Main Execution ---

def main_loop(args):
    """Main loop: Periodically reads I2C, processes sensor data."""
    global last_sensor_send_time, mqtt_connected, mqtt_connection_attempted
    log.info("Starting main processing loop...")
    last_mqtt_check_time = 0

    while True:
        try:
            now = time.time()

            # --- Check/Reconnect MQTT if needed ---
            if mqtt_client and not mqtt_connected and mqtt_connection_attempted and (now - last_mqtt_check_time > 30): # Check every 30s if disconnected
                 log.info("Attempting to reconnect MQTT...")
                 try:
                      # Use reconnect_async for non-blocking reconnect attempt
                      mqtt_client.reconnect_async()
                 except Exception as recon_e:
                      log.error(f"MQTT reconnect attempt failed: {recon_e}")
                 last_mqtt_check_time = now

            # --- Periodically Read I2C Data ---
            i2c_data = read_i2c_data(args.slave_addr, args.max_read_len)

            if i2c_data:
                if not isinstance(i2c_data, dict): log.warning(f"Received non-dict data from I2C: {i2c_data}"); continue
                data_type = i2c_data.get("type")
                log.debug(f"Parsed I2C data: Type='{data_type}', Data={i2c_data}")

                if data_type == "sensor_data":
                    # Sensor data received - check if time to send/log/publish
                    if now - last_sensor_send_time >= args.read_interval:
                        process_and_distribute_sensor_data(
                            i2c_data,
                            meshtastic_interface,
                            args.mesh_portnum,
                            args.mqtt_topic,
                            args.mqtt_qos,
                            args.mqtt_retain
                        )
                    else:
                        # Log even if not sending immediately? Yes, log on receipt.
                        log.debug(f"Sensor data read (Seq: {i2c_data.get('sequence', 'N/A')}), waiting for send interval.")
                        if sensor_logger:
                             if 'host_ts' not in i2c_data: i2c_data['host_ts'] = int(time.time())
                             try:
                                  sensor_logger.info(json.dumps(i2c_data, separators=(',', ':')))
                             except Exception as log_e:
                                  log.error(f"Failed to write sensor data to log file (waiting interval): {log_e}")


                elif data_type == "acknowledgment": log.info(f"Read stray acknowledgment from I2C in main loop: {i2c_data}")
                elif data_type == "error": log.error(f"Received unsolicited error message from I2C slave: {i2c_data.get('msg', 'Unknown error')}")
                else: log.warning(f"Received unknown data type '{data_type}' from I2C slave: {i2c_data}")

            # --- Sleep before next cycle ---
            time.sleep(args.poll_interval)

        except KeyboardInterrupt: log.info("Keyboard interrupt received. Exiting main loop."); break
        except Exception as e: log.error(f"Unhandled error in main loop: {e}", exc_info=True); log.info("Sleeping for 5 seconds after error..."); time.sleep(5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Akita Switch Meshtastic <-> I2C Bridge (v4). Relays commands, logs/sends sensor data, optional MQTT.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    # --- Argument Groups ---
    parser_serial = parser.add_argument_group('Serial Port')
    parser_i2c = parser.add_argument_group('I2C Settings')
    parser_mesh = parser.add_argument_group('Meshtastic Settings')
    parser_mqtt = parser.add_argument_group('MQTT Settings (Optional)')
    parser_timing = parser.add_argument_group('Timing')
    parser_logging = parser.add_argument_group('Logging')

    # Serial Port Argument
    parser_serial.add_argument('--port', help="Meshtastic device serial port. Auto-detect if omitted.")
    # I2C Arguments
    parser_i2c.add_argument('--i2c-bus', type=int, default=DEFAULT_I2C_BUS, help="I2C bus number")
    parser_i2c.add_argument('--slave-addr', type=lambda x: int(x, 0), default=DEFAULT_SLAVE_ADDRESS, help="I2C slave address (hex or dec)")
    parser_i2c.add_argument('--max-read-len', type=int, default=DEFAULT_MAX_I2C_READ_LEN, help="Max bytes to read from I2C slave")
    # Meshtastic Arguments
    parser_mesh.add_argument('--mesh-portnum', type=int, default=DEFAULT_MESH_PORTNUM, help="Meshtastic PortNum for Akita Switch")
    # MQTT Arguments
    parser_mqtt.add_argument('--mqtt-broker', default=DEFAULT_MQTT_BROKER, help="MQTT broker hostname or IP address. Enables MQTT if set.")
    parser_mqtt.add_argument('--mqtt-port', type=int, default=DEFAULT_MQTT_PORT, help="MQTT broker port")
    parser_mqtt.add_argument('--mqtt-topic', default=DEFAULT_MQTT_TOPIC, help="MQTT base topic to publish sensor data to")
    parser_mqtt.add_argument('--mqtt-user', default=DEFAULT_MQTT_USER, help="MQTT username (optional)")
    parser_mqtt.add_argument('--mqtt-pass', default=DEFAULT_MQTT_PASS, help="MQTT password (optional)")
    parser_mqtt.add_argument('--mqtt-client-id', default=DEFAULT_MQTT_CLIENT_ID, help="MQTT client ID")
    parser_mqtt.add_argument('--mqtt-qos', type=int, default=DEFAULT_MQTT_QOS, choices=[0, 1, 2], help="MQTT Quality of Service level for publishing")
    parser_mqtt.add_argument('--mqtt-retain', action='store_true', default=DEFAULT_MQTT_RETAIN, help="Enable MQTT retain flag for published messages")
    parser_mqtt.add_argument('--mqtt-tls', action='store_true', default=DEFAULT_MQTT_TLS, help="Enable TLS/SSL for MQTT connection")
    parser_mqtt.add_argument('--mqtt-tls-insecure', action='store_true', default=DEFAULT_MQTT_TLS_INSECURE, help="Skip MQTT TLS hostname verification (use with caution!)")
    parser_mqtt.add_argument('--mqtt-ca-certs', default=DEFAULT_MQTT_CA_CERTS, help="Path to custom CA certificate file for MQTT TLS")
    # Timing Arguments
    parser_timing.add_argument('--read-interval', type=float, default=DEFAULT_SENSOR_READ_INTERVAL_S, help="Interval (s) to read/send sensor data")
    parser_timing.add_argument('--poll-interval', type=float, default=DEFAULT_POLL_INTERVAL_S, help="Polling interval (s) for main loop checks")
    parser_timing.add_argument('--timeout', type=float, default=DEFAULT_COMMAND_TIMEOUT_S, help="Timeout (s) waiting for I2C command ACK")
    # Logging Arguments
    parser_logging.add_argument('--log-level', default=DEFAULT_LOG_LEVEL, choices=['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'], help='Set console logging verbosity')
    parser_logging.add_argument('--sensor-log', default=DEFAULT_SENSOR_LOG_FILE, help="Path to sensor data log file (JSON Lines format)")
    parser_logging.add_argument('--sensor-log-mb', type=float, default=DEFAULT_SENSOR_LOG_MAX_BYTES / (1024*1024), help="Max size (MB) for sensor log file before rotation")
    parser_logging.add_argument('--sensor-log-count', type=int, default=DEFAULT_SENSOR_LOG_BACKUP_COUNT, help="Number of backup sensor log files to keep")

    args = parser.parse_args()

    # Update console logging level
    log_level_upper = args.log_level.upper()
    log.setLevel(log_level_upper)
    # Set root logger level if desired (e.g., to see library logs)
    # logging.getLogger().setLevel(log_level_upper)
    log.info("--- Akita Switch Host Bridge Starting (v4) ---")
    log.info(f"Version using meshtastic lib {meshtastic.__version__}, paho-mqtt lib {paho.__version__}")
    log.info(f"Full configuration: {vars(args)}") # Log parsed args
    log.info("Copyright (C) 2025 Akita Engineering. License: GPLv3.")

    # Setup sensor data file logging
    setup_sensor_logging(args.sensor_log, int(args.sensor_log_mb * 1024 * 1024), args.sensor_log_count)

    # Setup MQTT if broker is specified
    mqtt_enabled = setup_mqtt(args)

    # Initialize I2C and Meshtastic connections
    if setup_i2c(args.i2c_bus, args.slave_addr) and setup_meshtastic(args.port):
        try:
            main_loop(args) # Start the main processing loop
        except Exception as e: log.critical(f"Main loop exited unexpectedly: {e}", exc_info=True)
        finally:
            # Cleanup resources
            log.info("Shutting down...")
            if meshtastic_interface: log.info("Closing Meshtastic interface."); meshtastic_interface.close()
            if i2c_bus: log.info("Closing I2C bus."); i2c_bus.close()
            if mqtt_client: log.info("Disconnecting MQTT client."); mqtt_client.loop_stop(); mqtt_client.disconnect()
            logging.shutdown() # Ensure all handlers are closed properly
            log.info("--- Akita Switch Host Bridge Stopped ---")
            sys.exit(0)
    else:
        log.critical("Initialization failed. Check config/connections. Exiting.")
        # Ensure resources are closed even if init failed partially
        if meshtastic_interface: meshtastic_interface.close()
        if i2c_bus: i2c_bus.close()
        if mqtt_client: mqtt_client.loop_stop(); mqtt_client.disconnect() # Stop MQTT loop if started
        sys.exit(1)
