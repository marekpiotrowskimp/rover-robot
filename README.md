# ROVER — conversational home robot

A talking robot built on **LEGO Mindstorms NXT 1.0** (tracked chassis) and a **Seeed XIAO ESP32-S3 Sense** (OV3660 camera, PDM microphone). You talk to it in Polish; an LLM agent decides what to say and how to move, and drives the NXT motors over Bluetooth.

> The robot speaks Polish, so prompts and log messages in the code are in Polish.
> The original, more extensive design document (in Polish) is in [`docs/DESIGN_PL.md`](docs/DESIGN_PL.md) — it describes the target architecture, of which only part is implemented (see [Status](#status)).

## How it works

```
 ┌──────────────── XIAO ESP32-S3 Sense ────────────────┐
 │ PDM mic → VAD → WAV ──── POST /api/speak ──────────►│──┐
 │ I2S amp + speaker ◄──── WAV reply ──────────────────│  │
 │ OV3660 camera → GET /snap (JPEG) ◄──────────────────│──┤  Wi-Fi (LAN)
 │ polls GET /api/cmd, reports POST /api/event ───────►│  │
 └─────────────────────────────────────────────────────┘  │
                                                          ▼
 ┌──────────────── Backend (macOS, FastAPI :8000) ─────────────────────────┐
 │ stt.py   faster-whisper (local)            → Polish transcript           │
 │ llm.py   ReAct agent loop via the GitHub Copilot Chat API                │
 │          (model claude-sonnet-4), optional camera snapshot as vision     │
 │          → JSON step: {action, duration_ms, say, done}                   │
 │ tts.py   macOS `say` + ffmpeg → WAV 16 kHz / 16-bit / mono               │
 │ nxt_control.py  Bluetooth (IOBluetooth / RFCOMM serial) ─────────────────│──► LEGO NXT brick
 └──────────────────────────────────────────────────────────────────────────┘     port B = left track
                                                                                  port C = right track
```

Agent actions: `forward`, `backward`, `turn_left`, `turn_right`, `stop`, `look`, `done`. The agent loops step by step until the task is done or impossible (it gives up after 3 attempts without progress).

## Hardware

| Part | Model | Role |
|---|---|---|
| Microcontroller + camera | Seeed XIAO ESP32-S3 Sense | 240 MHz, 8 MB PSRAM, Wi-Fi, BT 5.0, OV3660 camera, PDM microphone |
| Heatsink | XIAO ESP32-S3 heatsink kit | cooling under heavy Wi-Fi/camera load |
| Chassis + motors | LEGO Mindstorms NXT 1.0 | NXT brick (BT 2.0), 2× NXT motors with encoders, Technic tracks |
| Audio amplifier | Adafruit Audio BFF for XIAO (5769, MAX98357A, I2S, 3 W) | mounts back-to-back on the XIAO pins |
| Speaker | 4 Ω, 3 W, ~40 mm | voice output |

## Repository layout

```
backend/
  main.py             FastAPI app: /api/speak, /api/event, /api/cmd, /api/status
  llm.py              agent loop + system prompts (GitHub Copilot Chat API)
  copilot_auth.py     reads the GitHub OAuth token stored by OpenCode
  stt.py              speech-to-text (faster-whisper, "tiny" model by default)
  tts.py              text-to-speech (macOS `say` + ffmpeg → WAV)
  nxt_control.py      NXT Direct Commands over Bluetooth (macOS)
  start.sh            starts the backend (stops a previous instance cleanly first)
  test_*.py           manual hardware tests (NXT ports, raw commands, tone, RFCOMM, serial)
firmware/
  rover_firmware/     main firmware: mic capture + VAD, I2S audio player, camera (snapshot/MJPEG),
                      HTTP server, Wi-Fi transport, state machine, NXT Bluetooth module
  rover_minimal/      Wi-Fi-only sketch (no camera/I2S/mic) for diagnosing brown-out resets
  empty_sketch/       empty sketch with the brown-out detector disabled (diagnostics)
docs/DESIGN_PL.md     full design document (Polish)
```

## Running

### Backend (macOS)

Requirements: Python 3.10+, `ffmpeg` (`brew install ffmpeg`), the NXT brick paired with the Mac over Bluetooth, and a GitHub Copilot subscription with a token stored by [OpenCode](https://opencode.ai) (`~/.local/share/opencode/auth.json`).

```sh
cd backend
python3 -m venv venv && venv/bin/pip install -r requirements.txt
./start.sh                    # plays the reply locally as well (default)
./start.sh --no-local-audio   # reply only on the robot
```

The ESP32 camera address used for vision is set in `backend/main.py` (`ESP32_SNAP_URL`).

### Firmware (XIAO ESP32-S3 Sense)

1. Copy `firmware/rover_firmware/secrets.h.example` to `secrets.h` and fill in your Wi-Fi credentials (the file is git-ignored).
2. Set the static ESP32 IP and the backend address in `firmware/rover_firmware/config.h` (`ESP32_STATIC_IP`, `BACKEND_HOST`).
3. Build and flash with Arduino IDE / `arduino-cli` for `XIAO_ESP32S3` with PSRAM enabled (OPI).

## NXT Bluetooth protocol (summary)

NXT 1.0 uses Bluetooth Classic 2.0 with the Serial Port Profile. Each Direct Command is a byte packet preceded by a 2-byte little-endian length header. Motors are driven with `SET_OUTPUT_STATE` (`0x04`):

```
[0x80, 0x04, port, power_s8, mode, regulation_mode, turn_ratio, run_state, tacho_limit_u32_LE]
 port: 0x01 = B (left), 0x02 = C (right), 0xFF = all     power: -100..100
```

Full protocol notes and examples: [`docs/DESIGN_PL.md`](docs/DESIGN_PL.md#protokół-bluetooth-nxt).

## Status

| Area | State |
|---|---|
| Voice loop: microphone → STT → agent → TTS → speaker | implemented |
| Movement over Bluetooth (forward/backward/turns/stop) | implemented |
| Camera snapshot as context for the agent (vision) | implemented (optional) |
| Persistent memory (SQLite), autonomous scheduled tasks, Web UI dashboard | designed in `docs/DESIGN_PL.md`, not implemented yet |

## License

MIT — see [LICENSE](LICENSE).
