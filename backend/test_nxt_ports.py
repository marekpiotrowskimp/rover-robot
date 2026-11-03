"""
Test: GET_BATTERY_LEVEL z response=True — NXT powinien odpowiedzieć poziomem baterii.
Jeśli dostaniemy odpowiedź, BT działa. Testuje też SET_OUTPUT_STATE na wszystkich portach.
"""
import sys
import os
import struct
import time

sys.path.insert(0, os.path.dirname(__file__))
import nxt_control

print("Łączę z NXT...")
ok = nxt_control.connect()
if not ok:
    print("BŁĄD: nie można połączyć")
    sys.exit(1)

print("Połączono! Czekam 1s...")
time.sleep(1)

fd = nxt_control._fd

# Test 1: GET_BATTERY_LEVEL z response=True (0x00)
print("\n--- TEST 1: GET_BATTERY_LEVEL (oczekuję odpowiedzi) ---")
cmd = bytes([0x00, 0x0B])  # response=True, GET_BATTERY_LEVEL
packet = struct.pack("<H", len(cmd)) + cmd
print(f"Wysyłam: {packet.hex()}")
os.write(fd, packet)
time.sleep(0.3)
try:
    response = os.read(fd, 64)
    print(f"Odpowiedź: {response.hex()}")
    if len(response) >= 6:
        mv = struct.unpack_from("<H", response, 4)[0]
        print(f"Napięcie baterii: {mv} mV ({mv/1000:.2f} V)")
    else:
        print("Za krótka odpowiedź")
except OSError as e:
    print(f"Brak odpowiedzi / błąd: {e}")

time.sleep(0.5)

# Test 2: SET_OUTPUT_STATE port A (0x00) — może silniki są na A i C?
print("\n--- TEST 2: Silnik PORT A (0x00) power=70 ---")
cmd_a = bytes([0x80, 0x04, 0x00, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
packet_a = struct.pack("<H", len(cmd_a)) + cmd_a
print(f"Wysyłam port A: {packet_a.hex()}")
os.write(fd, packet_a)
time.sleep(2.0)
# stop port A
cmd_stop = bytes([0x80, 0x04, 0x00, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
os.write(fd, struct.pack("<H", len(cmd_stop)) + cmd_stop)
print("PORT A stop")

time.sleep(0.5)

# Test 3: SET_OUTPUT_STATE port B (0x01)
print("\n--- TEST 3: Silnik PORT B (0x01) power=70 ---")
cmd_b = bytes([0x80, 0x04, 0x01, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
packet_b = struct.pack("<H", len(cmd_b)) + cmd_b
print(f"Wysyłam port B: {packet_b.hex()}")
os.write(fd, packet_b)
time.sleep(2.0)
cmd_stop2 = bytes([0x80, 0x04, 0x01, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
os.write(fd, struct.pack("<H", len(cmd_stop2)) + cmd_stop2)
print("PORT B stop")

time.sleep(0.5)

# Test 4: SET_OUTPUT_STATE port C (0x02)
print("\n--- TEST 4: Silnik PORT C (0x02) power=70 ---")
cmd_c = bytes([0x80, 0x04, 0x02, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
packet_c = struct.pack("<H", len(cmd_c)) + cmd_c
print(f"Wysyłam port C: {packet_c.hex()}")
os.write(fd, packet_c)
time.sleep(2.0)
cmd_stop3 = bytes([0x80, 0x04, 0x02, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
os.write(fd, struct.pack("<H", len(cmd_stop3)) + cmd_stop3)
print("PORT C stop")

time.sleep(0.5)

# Test 5: ALL ports (0xFF)
print("\n--- TEST 5: WSZYSTKIE porty (0xFF) power=70 ---")
cmd_all = bytes([0x80, 0x04, 0xFF, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
packet_all = struct.pack("<H", len(cmd_all)) + cmd_all
print(f"Wysyłam port ALL: {packet_all.hex()}")
os.write(fd, packet_all)
time.sleep(2.0)
cmd_stop4 = bytes([0x80, 0x04, 0xFF, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
os.write(fd, struct.pack("<H", len(cmd_stop4)) + cmd_stop4)
print("ALL stop")

print("\nGotowe. Który port ruszył silnik?")
nxt_control.disconnect()
