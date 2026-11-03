"""
Test: wysyła PLAY_TONE do NXT — jeśli NXT piśnie, BT działa poprawnie.
Jeśli nie piśnie, problem jest w połączeniu/protokole.

PLAY_TONE: opcode 0x03
  [0x80, 0x03, freq_lo, freq_hi, duration_lo, duration_hi]
  freq = 1000 Hz, duration = 500 ms
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

# PLAY_TONE 1000 Hz, 500ms (no-response)
freq = 1000
duration_ms = 500
cmd = bytes([0x80, 0x03]) + struct.pack("<H", freq) + struct.pack("<H", duration_ms)
print(f"Wysyłam PLAY_TONE {freq}Hz {duration_ms}ms: {cmd.hex()}")
nxt_control._send_raw(cmd)
time.sleep(0.5)

# Drugi ton — 2000 Hz
freq2 = 2000
cmd2 = bytes([0x80, 0x03]) + struct.pack("<H", freq2) + struct.pack("<H", duration_ms)
print(f"Wysyłam PLAY_TONE {freq2}Hz {duration_ms}ms: {cmd2.hex()}")
nxt_control._send_raw(cmd2)
time.sleep(0.5)

print("Czy słyszałeś dwa tony? (tak/nie)")
nxt_control.disconnect()
