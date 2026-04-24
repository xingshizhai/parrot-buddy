import serial
import time
import sys

MAX_RETRIES = 5
RETRY_DELAY = 2

for attempt in range(MAX_RETRIES):
    try:
        ser = serial.Serial('COM3', 115200, timeout=1)
        print(f"Connected to COM3 at 115200 baud")
        break
    except serial.SerialException as e:
        if attempt < MAX_RETRIES - 1:
            print(f"Port busy, retrying in {RETRY_DELAY}s... ({attempt+1}/{MAX_RETRIES})")
            time.sleep(RETRY_DELAY)
        else:
            print(f"Failed to connect: {e}")
            sys.exit(1)

output_file = open('D:/Work/esp32/projects/parrot-buddy/serial_log.txt', 'w', encoding='utf-8')
output_file.write("=== SERIAL MONITOR STARTED ===\n")
output_file.flush()

print("Monitoring... (Ctrl+C to stop)")
try:
    while True:
        data = ser.readline()
        if data:
            try:
                line = data.decode('utf-8', errors='ignore').strip()
                print(line)
                output_file.write(line + '\n')
                output_file.flush()
            except Exception as e:
                hex_str = data.hex()
                print(f"HEX: {hex_str}")
                output_file.write(f"HEX: {hex_str}\n")
                output_file.flush()
        time.sleep(0.01)
except KeyboardInterrupt:
    ser.close()
except Exception as e:
    print(f"Error: {e}")
    ser.close()
finally:
    output_file.write("=== SERIAL MONITOR STOPPED ===\n")
    output_file.close()
    print("\nDisconnected, log saved to serial_log.txt")
