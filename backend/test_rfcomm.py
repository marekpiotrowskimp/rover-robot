"""
Test strategii 2 (RFCOMM) — sprawdza czy NXT widzi połączenie i odpowiada.
"""
import sys
import os
import struct
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))
import nxt_control

print("Łączę przez RFCOMM (strategia 2)...")
ok = nxt_control.connect()
if not ok:
    print("BŁĄD: nie można połączyć")
    sys.exit(1)

ch = nxt_control._channel
print(f"channel: {ch}")
print(f"isOpen: {ch.isOpen() if ch else 'N/A'}")
print(f"_is_connected: {nxt_control._is_connected()}")

print("\nCzekam 2s — sprawdź czy ikona BT pojawia się na NXT...")
time.sleep(2)

# GET_BATTERY_LEVEL z response=True przez RFCOMM
print("\n--- GET_BATTERY_LEVEL przez RFCOMM ---")
cmd = bytes([0x00, 0x0B])
packet = struct.pack("<H", len(cmd)) + cmd
print(f"Wysyłam: {packet.hex()}")

# Włącz odbieranie odpowiedzi przez _recv_buf
nxt_control._recv_event.clear()
ch.writeSync_length_(packet, len(packet))

# Czekaj na odpowiedź w _recv_buf
got = nxt_control._recv_event.wait(timeout=3.0)
if got:
    with nxt_control._recv_lock:
        response = bytes(nxt_control._recv_buf)
        nxt_control._recv_buf.clear()
    print(f"Odpowiedź: {response.hex()}")
    if len(response) >= 6:
        mv = struct.unpack_from("<H", response, 4)[0]
        print(f"Bateria: {mv} mV ({mv/1000:.2f} V) — RFCOMM DZIAŁA!")
    else:
        print(f"Odpowiedź zbyt krótka: {len(response)} bajtów")
else:
    print("BRAK ODPOWIEDZI po 3s")

time.sleep(0.5)

# Test silnika B
print("\n--- SET_OUTPUT_STATE port B power=70 (2s) ---")
cmd_b = bytes([0x80, 0x04, 0x01, 70, 0x01, 0x00, 0x00, 0x20, 0,0,0,0])
pkt = struct.pack("<H", len(cmd_b)) + cmd_b
ch.writeSync_length_(pkt, len(pkt))
print("Wysłano — obserwuj czy silnik B się rusza...")
time.sleep(2.0)

cmd_stop = bytes([0x80, 0x04, 0x01, 0, 0x00, 0x00, 0x00, 0x00, 0,0,0,0])
ch.writeSync_length_(struct.pack("<H", len(cmd_stop)) + cmd_stop, len(cmd_stop) + 2)
print("Stop")

nxt_control.disconnect()
