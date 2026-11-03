"""
Test: wyłącza reader thread, wysyła GET_BATTERY_LEVEL z response=True,
czeka na odpowiedź. Jeśli NXT odpowie poprawnie — BT działa.
Następnie próbuje silniki.
"""
import sys
import os
import struct
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))

# Zatrzymaj reader thread zanim connect() go uruchomi — patch
import nxt_control

print("Łączę z NXT...")
ok = nxt_control.connect()
if not ok:
    print("BŁĄD: nie można połączyć")
    sys.exit(1)

# Zatrzymaj reader thread żeby nie kradł odpowiedzi
print("Zatrzymuję reader thread...")
nxt_control._reader_stop.set()
time.sleep(0.3)

fd = nxt_control._fd
if fd is None:
    print("BŁĄD: fd=None")
    sys.exit(1)

print(f"fd={fd}, połączono przez serial")

# Opróżnij bufor
try:
    while True:
        d = os.read(fd, 256)
        if not d:
            break
except OSError:
    pass

time.sleep(0.5)

# GET_BATTERY_LEVEL z response=True
print("\n--- GET_BATTERY_LEVEL (response=True, bez reader thread) ---")
cmd = bytes([0x00, 0x0B])
packet = struct.pack("<H", len(cmd)) + cmd
print(f"Wysyłam: {packet.hex()}")
os.write(fd, packet)

# Czekaj na odpowiedź
time.sleep(0.5)
response = b""
deadline = time.time() + 2.0
while time.time() < deadline:
    try:
        chunk = os.read(fd, 64)
        if chunk:
            response += chunk
            print(f"  chunk: {chunk.hex()}")
    except OSError as e:
        if e.errno in (11, 35):  # EAGAIN/EWOULDBLOCK
            time.sleep(0.05)
        else:
            print(f"  read error: {e}")
            break

if response:
    print(f"Pełna odpowiedź: {response.hex()}")
    if len(response) >= 6:
        mv = struct.unpack_from("<H", response, 4)[0]
        print(f"Bateria: {mv} mV ({mv/1000:.2f} V) — BT działa w obie strony!")
else:
    print("BRAK ODPOWIEDZI — NXT nie odpowiada lub bajty nie dochodzą")

time.sleep(0.5)

# Teraz silnik port B
print("\n--- SET_OUTPUT_STATE port B power=70 (2s) ---")
cmd_b = bytes([0x80, 0x04, 0x01, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
pkt = struct.pack("<H", len(cmd_b)) + cmd_b
print(f"Wysyłam: {pkt.hex()}")
n = os.write(fd, pkt)
print(f"os.write zwrócił: {n} bajtów")
time.sleep(2.0)

# Stop
cmd_stop = bytes([0x80, 0x04, 0x01, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
os.write(fd, struct.pack("<H", len(cmd_stop)) + cmd_stop)
print("Stop wysłany")

print("\nCzy silnik B się ruszył?")
nxt_control.disconnect()
