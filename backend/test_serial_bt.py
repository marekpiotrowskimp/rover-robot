#!/usr/bin/env python3
"""
Test standalone strategii serial: openConnection() + /dev/cu.RoboC O_NONBLOCK.

Uruchom z katalogu backend:
  cd backend && source venv/bin/activate && python test_serial_bt.py

Jeśli zakończy się sukcesem, robot wykona obrót 180° lewo + 180° prawo.
"""

import logging
import sys
import os
import time

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)

sys.path.insert(0, os.path.dirname(__file__))
import nxt_control

print("=== NXT Serial BT Test ===")
print(f"NXT_MAC: {nxt_control.NXT_MAC}")
print(f"NXT_DEV: {nxt_control.NXT_DEV}")
print(f"/dev/cu.RoboC exists: {os.path.exists(nxt_control.NXT_DEV)}")
print()

print("Connecting...")
ok = nxt_control.connect()
if not ok:
    print("FAILED: connect() returned False")
    sys.exit(1)

print("Connected! Running test_spin...")
nxt_control.test_spin()

print()
print("Test done. Disconnecting...")
nxt_control.disconnect()
print("Done.")
