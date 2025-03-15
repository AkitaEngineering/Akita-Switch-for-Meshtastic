import meshtastic
import meshtastic.serial_interface
import time
from smbus2 import SMBus, I2cError
import json

bus = SMBus(1)
SLAVE_ADDRESS = 0x04

def send_command(command):
    try:
        bus.write_i2c_block_data(SLAVE_ADDRESS, 0, bytes(json.dumps(command), 'utf-8'))
        print(f"Sent command: {command}")
        start_time = time.time()
        while time.time() - start_time < 5:
            ack = read_acknowledgment()
            if ack and ack.get("sequence") == command.get("sequence"):
                if ack.get("status") == "success":
                    print("Command acknowledged.")
                    return True
                else:
                    print("command failed.")
                    return False
            time.sleep(0.2)
        print("Command timed out.")
        return False
    except I2cError as e:
        print(f"I2C error sending command: {e}")
        return False

def read_acknowledgment():
    data = read_sensor_data()
    if data and data.get("type") == "acknowledgment":
        return data
    return None

def on_receive(packet, interface):
    if packet['decoded']['portnum'] == meshtastic.PORTS.TEXT_MESSAGE_APP:
        payload = packet['decoded']['text']
        print(f"Received Meshtastic: {payload}")
        try:
            command = json.loads(payload)
            send_command(command)
        except json.JSONDecodeError:
            print("Invalid JSON received")

def read_sensor_data():
    try:
        data = bus.read_i2c_block_data(SLAVE_ADDRESS, 0, 256)
        json_string = bytes(data).decode('utf-8').rstrip('\x00')
        if json_string:
            try:
                sensor_data = json.loads(json_string)
                if sensor_data.get("type") == "sensor_data":
                    return sensor_data
                elif sensor_data.get("type") == "acknowledgment":
                    return sensor_data
                else:
                    return None
            except json.JSONDecodeError:
                print("Invalid JSON from sensor")
                return None
        else:
            return None
    except I2cError as e:
        print(f"I2C error reading sensor: {e}")
        return None

try:
    interface = meshtastic.serial_interface.SerialInterface()
    interface.add_receive_callback(on_receive)
    print("Meshtastic interface started.")
except Exception as e:
    print(f"Meshtastic interface error: {e}")
    exit()

while True:
    sensor_data = read_sensor_data()
    if sensor_data and sensor_data.get("type") == "sensor_data":
        try:
            interface.sendText(json.dumps(sensor_data))
        except Exception as e:
            print(f"Meshtastic send error: {e}")
    time.sleep(60)
