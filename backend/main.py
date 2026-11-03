"""
ROVER Backend - FastAPI server
Listens on 0.0.0.0:8000

Endpoints:
  POST /api/speak   - Receive WAV from ESP32, run Agent ReAct loop, return WAV
  POST /api/event   - Receive events from ESP32 (state changes, errors)
  GET  /api/cmd     - ESP32 polls for pending movement commands
  GET  /api/status  - Health check + current state
"""

import asyncio
import logging
import os
import queue
import subprocess
import tempfile
import threading
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.responses import JSONResponse

import nxt_control
import stt
import llm
import tts

# Odtwarzaj odpowiedź lokalnie na backendzie (macOS afplay).
LOCAL_PLAYBACK: bool = os.environ.get("LOCAL_PLAYBACK", "1") != "0"

# URL kamery ESP32 (statyczne IP ustawione w firmware)
ESP32_SNAP_URL = "http://192.168.0.189/snap"

# Maksymalna liczba kroków agenta na jedno zadanie
AGENT_MAX_STEPS = 15

# Timeout całej pętli agenta (sekundy)
AGENT_TOTAL_TIMEOUT = 120.0

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logging.getLogger("nxt_control").setLevel(logging.DEBUG)
logger = logging.getLogger("rover.main")

# Semafor: tylko jedno odtwarzanie naraz
_playback_lock = threading.Lock()

# Semafor: tylko jedna pętla agenta naraz (nie nakładamy zadań)
_agent_lock = threading.Lock()

_cmd_queue: queue.Queue = queue.Queue(maxsize=10)


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info(f"[ROVER] Backend starting up... LOCAL_PLAYBACK={'on' if LOCAL_PLAYBACK else 'off'}")
    llm.ESP32_SNAP_URL = ESP32_SNAP_URL
    logger.info(f"[Vision] ESP32 snap URL: {ESP32_SNAP_URL}")
    threading.Thread(target=_try_nxt_connect, daemon=True).start()
    yield
    logger.info("[ROVER] Backend shutting down...")
    nxt_control.stop()       # zatrzymaj silniki
    nxt_control.disconnect() # zamknij kanał RFCOMM (żeby macOS nie zostawił zombie BT)


def _try_nxt_connect():
    ok = nxt_control.connect()
    if ok:
        logger.info("[NXT] Connected to brick")
        threading.Thread(target=nxt_control.test_spin, daemon=True).start()
    else:
        logger.warning("[NXT] Brick not found — movement commands will be logged only")


app = FastAPI(title="ROVER Backend", lifespan=lifespan)


# ---------------------------------------------------------------------------
# POST /api/speak
# ---------------------------------------------------------------------------

@app.post("/api/speak")
async def speak(request: Request):
    """
    Receive raw WAV audio from ESP32.
    Pipeline: WAV -> STT -> Agent ReAct loop -> TTS -> WAV response.
    """
    wav_bytes = await request.body()
    if not wav_bytes:
        raise HTTPException(status_code=400, detail="Empty audio")

    logger.info(f"[/api/speak] Received {len(wav_bytes)} bytes WAV")

    loop = asyncio.get_event_loop()
    response_wav = await loop.run_in_executor(None, _pipeline, wav_bytes)

    if not response_wav:
        return Response(status_code=204)

    return Response(content=response_wav, media_type="audio/wav")


def _pipeline(wav_bytes: bytes) -> bytes:
    """STT -> Agent ReAct loop -> TTS (runs in thread pool)."""
    # 1. Transcribe
    user_text = stt.transcribe_wav(wav_bytes)
    if not user_text:
        logger.info("[Pipeline] Empty transcript — ignoring")
        return b""

    logger.info(f"[Pipeline] Transcript: '{user_text}'")

    # 2. Agent ReAct loop
    reply_text = _agent_run(user_text)

    # 3. TTS
    if not reply_text:
        return b""

    wav_out = tts.text_to_wav(reply_text)

    # 4. Odtwórz lokalnie
    if LOCAL_PLAYBACK and wav_out:
        threading.Thread(target=_play_locally, args=(wav_out,), daemon=True).start()

    return wav_out


def _agent_run(goal: str) -> str:
    """
    Pętla ReAct agenta.

    Każdy krok:
      1. Pobierz świeży snapshot z kamery
      2. Wyślij do LLM: cel + historia kroków + obraz
      3. Wykonaj akcję synchronicznie (czekaj na zakończenie ruchu)
      4. Jeśli done=True lub limit kroków → zwróć say jako wynik

    Zwraca tekst do TTS (may be empty string).
    """
    if not _agent_lock.acquire(blocking=False):
        logger.warning("[Agent] Inna pętla agenta w toku — ignoruję nowe zadanie")
        return "Chwileczkę, jeszcze kończę poprzednie zadanie."

    try:
        llm.agent_reset()
        logger.info(f"[Agent] START — cel: '{goal}'")

        deadline = time.time() + AGENT_TOTAL_TIMEOUT
        final_say = ""

        for step in range(1, AGENT_MAX_STEPS + 1):
            if time.time() > deadline:
                logger.warning("[Agent] Timeout!")
                nxt_control.stop()
                return "Czas minął, zatrzymuję się."

            # Snapshot z kamery (świeży — GRAB_LATEST w firmware)
            image_b64 = llm._fetch_snapshot(ESP32_SNAP_URL) if ESP32_SNAP_URL else None

            # Krok agenta
            result = llm.agent_step(goal, step, image_b64)

            say = (result.get("say") or "").strip()
            action = result.get("action", "stop")
            duration_ms = result.get("duration_ms", 1000)
            done = result.get("done", False)

            if say:
                final_say = say

            # Zakolejkuj komendę dla ESP32 /api/cmd (info)
            if action not in ("done", "look", "stop"):
                try:
                    _cmd_queue.put_nowait({"move": action, "duration_ms": duration_ms})
                except queue.Full:
                    pass

            # Wykonaj ruch synchronicznie
            if action == "done":
                nxt_control.stop()
                logger.info(f"[Agent] DONE po {step} krokach. Say: '{final_say}'")
                break
            elif action == "look":
                # Tylko obserwuj — stój i poczekaj na stabilizację kamery
                nxt_control.stop()
                time.sleep(0.8)
            else:
                nxt_control.execute_sync({"action": action, "duration_ms": duration_ms})
                # Poczekaj na stabilizację kamery po ruchu (obraz rozmazany tuż po zatrzymaniu)
                time.sleep(0.6)

            if done:
                logger.info(f"[Agent] DONE (flaga) po {step} krokach.")
                break

        else:
            # Przekroczono limit kroków
            logger.warning(f"[Agent] Limit {AGENT_MAX_STEPS} kroków osiągnięty")
            nxt_control.stop()
            if not final_say:
                final_say = "Osiągnąłem limit kroków, zatrzymuję się."

        return final_say

    except Exception as e:
        logger.error(f"[Agent] Błąd w pętli: {e}", exc_info=True)
        nxt_control.stop()
        return "Przepraszam, wystąpił błąd."
    finally:
        _agent_lock.release()


def _play_locally(wav_bytes: bytes) -> None:
    """Odtwórz WAV przez głośnik Maca. Blokuje jeśli inne odtwarzanie w toku."""
    with _playback_lock:
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                f.write(wav_bytes)
                tmp_path = f.name
            subprocess.run(["afplay", tmp_path], check=True, capture_output=True)
            os.unlink(tmp_path)
            logger.info("[Audio] Local playback done")
        except Exception as e:
            logger.warning(f"[Audio] Local playback failed: {e}")


# ---------------------------------------------------------------------------
# POST /api/event
# ---------------------------------------------------------------------------

@app.post("/api/event")
async def event(request: Request):
    body = await request.json()
    logger.info(f"[/api/event] {body}")
    return JSONResponse({"ok": True})


# ---------------------------------------------------------------------------
# GET /api/cmd
# ---------------------------------------------------------------------------

@app.get("/api/cmd")
async def cmd():
    try:
        move_cmd = _cmd_queue.get_nowait()
        logger.info(f"[/api/cmd] Delivering: {move_cmd}")
        return JSONResponse(move_cmd)
    except queue.Empty:
        return JSONResponse({"move": "none"})


# ---------------------------------------------------------------------------
# GET /api/status
# ---------------------------------------------------------------------------

@app.get("/api/status")
async def status():
    return JSONResponse({
        "status": "ok",
        "nxt_connected": nxt_control._channel is not None or nxt_control._fd is not None,
        "pending_cmds": _cmd_queue.qsize(),
        "agent_busy": _agent_lock.locked(),
    })


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")
