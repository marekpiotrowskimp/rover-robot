"""
LLM conversation via GitHub Copilot (Claude / GPT-4o models).

Dwa tryby:
  agent_step() — jeden krok pętli ReAct agenta (zawsze JSON)
  chat()       — legacy single-turn (zachowany dla kompatybilności)
"""

import base64
import json
import re
import urllib.request
import urllib.error
import logging
from collections import deque

from copilot_auth import get_copilot_token, COPILOT_API_BASE

logger = logging.getLogger(__name__)

CHAT_URL = f"{COPILOT_API_BASE}/chat/completions"
MODEL = "claude-sonnet-4"  # available via GitHub Copilot Pro+

# URL do snapshotów kamery ESP32. Ustaw None żeby wyłączyć vision.
ESP32_SNAP_URL: str | None = None  # ustawiany z main.py po starcie

# ---------------------------------------------------------------------------
# System prompty
# ---------------------------------------------------------------------------

AGENT_SYSTEM_PROMPT = """Jesteś ROVER — autonomiczny robot domowy na podwoziu gąsienicowym LEGO NXT.
Masz kamerę i możesz się poruszać. Działasz w pętli ReAct: obserwujesz → myślisz → działasz.

ZAWSZE odpowiadaj WYŁĄCZNIE w formacie JSON (bez żadnego tekstu poza JSON):
{
  "thought": "co widzę i co zamierzam zrobić",
  "action": "<akcja>",
  "duration_ms": <czas w ms>,
  "done": <true|false>,
  "say": "<opcjonalny krótki komentarz po polsku, max 1 zdanie>"
}

Dostępne akcje:
  "forward"    — jedź do przodu
  "backward"   — jedź do tyłu
  "turn_left"  — obróć w lewo
  "turn_right" — obróć w prawo
  "stop"       — zatrzymaj się
  "look"       — stój i obserwuj (duration_ms ignorowane)
  "done"       — zakończ zadanie (cel osiągnięty lub niemożliwy)

Zasady:
- "done": true tylko gdy zadanie jest FAKTYCZNIE ukończone lub niemożliwe.
- "say" wypełniaj tylko gdy jest coś istotnego do powiedzenia (raport końcowy, ostrzeżenie).
- duration_ms: typowe wartości: skręt 90° ≈ 900ms, krok do przodu ≈ 800-1500ms.
- Przy zwykłej rozmowie (bez potrzeby ruchu): action="done", done=true, say=<odpowiedź>.
- Nie kręć się w kółko bez celu — jeśli po 3 próbach nie ma postępu, powiedz done=true z wyjaśnieniem."""

# Rolling context window dla trybu agenta (historia kroków w ramach zadania)
# Resetowana na początku każdego nowego zadania
_agent_history: list = []

# Legacy rolling history dla chat()
_chat_history: deque = deque(maxlen=20)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _fetch_snapshot(url: str, timeout: float = 2.0) -> str | None:
    """
    Pobierz JPEG z kamery ESP32 i zwróć jako base64 data URI.
    Zwraca None przy błędzie.
    """
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            jpeg_bytes = resp.read()
        if not jpeg_bytes:
            return None
        b64 = base64.b64encode(jpeg_bytes).decode("ascii")
        logger.info(f"[Vision] Snapshot: {len(jpeg_bytes)} bytes")
        return f"data:image/jpeg;base64,{b64}"
    except Exception as e:
        logger.warning(f"[Vision] Snapshot failed: {e}")
        return None


def _call_llm(messages: list, timeout: int = 30) -> str:
    """Wyślij messages do LLM i zwróć surowy tekst odpowiedzi."""
    payload = json.dumps({
        "model": MODEL,
        "messages": messages,
        "max_tokens": 512,
        "temperature": 0.3,
    }).encode()

    token = get_copilot_token()
    req = urllib.request.Request(
        CHAT_URL,
        data=payload,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "Copilot-Integration-Id": "vscode-chat",
            "Editor-Version": "vscode/1.85.0",
            "User-Agent": "rover-backend/1.0",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        result = json.loads(resp.read())
    return result["choices"][0]["message"]["content"]


def _parse_agent_json(text: str) -> dict:
    """
    Wyciągnij JSON z odpowiedzi LLM.
    LLM powinien zwracać czysty JSON, ale czasem opakowuje w ```json...```.
    """
    # Próba 1: czysty JSON
    text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    # Próba 2: JSON w bloku ```
    m = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.DOTALL)
    if m:
        try:
            return json.loads(m.group(1))
        except json.JSONDecodeError:
            pass

    # Próba 3: znajdź pierwszy { ... } w tekście
    m = re.search(r"\{.*\}", text, re.DOTALL)
    if m:
        try:
            return json.loads(m.group(0))
        except json.JSONDecodeError:
            pass

    logger.warning(f"[LLM] Nie udało się sparsować JSON: {text[:200]}")
    return {
        "thought": "błąd parsowania odpowiedzi",
        "action": "done",
        "duration_ms": 0,
        "done": True,
        "say": "Przepraszam, nie rozumiem.",
    }


# ---------------------------------------------------------------------------
# Agent ReAct — główne API
# ---------------------------------------------------------------------------

def agent_reset():
    """Zresetuj historię agenta (wywołaj przed nowym zadaniem)."""
    global _agent_history
    _agent_history = []
    logger.info("[Agent] Historia zresetowana")


def agent_step(goal: str, step_num: int, image_b64: str | None = None) -> dict:
    """
    Jeden krok pętli ReAct agenta.

    Args:
        goal:      cel/polecenie użytkownika (stały przez całą pętlę)
        step_num:  numer kroku (1-based, do loggowania)
        image_b64: base64 data URI aktualnego snapshotu z kamery (lub None)

    Returns:
        dict z kluczami: thought, action, duration_ms, done, say
    """
    # Zbuduj wiadomość user dla tego kroku
    if image_b64:
        user_content = [
            {"type": "image_url", "image_url": {"url": image_b64, "detail": "low"}},
            {"type": "text", "text": f"Krok {step_num}. Cel: {goal}\nCo widzisz? Co robisz dalej?"},
        ]
        # Do historii zapisujemy tylko tekst (bez base64 — oszczędzamy tokeny)
        user_text_for_history = f"Krok {step_num}. Cel: {goal} [obraz z kamery]"
    else:
        user_content = f"Krok {step_num}. Cel: {goal}\nBrak obrazu z kamery."
        user_text_for_history = user_content

    # Zbierz messages: system + historia + bieżący krok
    messages = [{"role": "system", "content": AGENT_SYSTEM_PROMPT}]
    messages.extend(_agent_history)
    messages.append({"role": "user", "content": user_content})

    try:
        reply_text = _call_llm(messages, timeout=30)
        logger.info(f"[Agent] Krok {step_num} raw reply: {reply_text[:200]}")
    except Exception as e:
        logger.error(f"[Agent] LLM call failed at step {step_num}: {e}")
        return {
            "thought": f"błąd LLM: {e}",
            "action": "done",
            "duration_ms": 0,
            "done": True,
            "say": "Przepraszam, błąd komunikacji z AI.",
        }

    result = _parse_agent_json(reply_text)

    # Walidacja i defaults
    result.setdefault("thought", "")
    result.setdefault("action", "stop")
    result.setdefault("duration_ms", 1000)
    result.setdefault("done", False)
    result.setdefault("say", "")
    # Sanitize: LLM może zwrócić null zamiast ""
    if result["say"] is None:
        result["say"] = ""

    # Jeśli action=done, wymuś done=True
    if result["action"] == "done":
        result["done"] = True

    logger.info(
        f"[Agent] Krok {step_num}: action={result['action']} "
        f"duration={result['duration_ms']}ms done={result['done']} "
        f"thought={result['thought'][:80]}"
    )

    # Dodaj do historii agenta (plain text, bez obrazu)
    _agent_history.append({"role": "user", "content": user_text_for_history})
    _agent_history.append({"role": "assistant", "content": reply_text})

    return result


# ---------------------------------------------------------------------------
# Legacy chat() — zachowany dla kompatybilności
# ---------------------------------------------------------------------------

LEGACY_SYSTEM_PROMPT = """Jesteś ROVER — robot domowy na podwoziu gąsienicowym LEGO NXT.
Odpowiadasz po polsku. Twoje odpowiedzi są bardzo krótkie — maksymalnie 1-2 zdania.
Kiedy użytkownik chce żebyś się poruszył lub zmienił prędkość, zwróć komendę w formacie JSON
w bloku ```json``` np.: ```json\n{"move": "forward", "duration_ms": 2000}\n```
Dostępne komendy ruchu: forward, backward, turn_left, turn_right, stop.
Zmiana prędkości: ```json\n{"move": "set_speed", "mode": "slow"}\n``` lub mode: "fast".
Poza tym rozmawiaj normalnie, krótko i rzeczowo."""


def _strip_json_blocks(text: str) -> str:
    return re.sub(r"```json\s*\{.*?\}\s*```", "", text, flags=re.DOTALL).strip()


def chat(user_text: str) -> tuple[str, dict | None]:
    """Legacy single-turn chat. Zachowany dla ewentualnego fallbacku."""
    _chat_history.append({"role": "user", "content": user_text})
    messages = [{"role": "system", "content": LEGACY_SYSTEM_PROMPT}] + list(_chat_history)

    try:
        reply = _call_llm(messages, timeout=30)
        _chat_history.append({"role": "assistant", "content": reply})
        logger.info(f"[LLM] Reply: {reply[:80]}")
        move_cmd = _extract_move(reply)
        spoken = _strip_json_blocks(reply)
        return spoken, move_cmd
    except Exception as e:
        logger.error(f"[LLM] Chat failed: {e}")
        return "Przepraszam, wystąpił błąd.", None


def _extract_move(text: str) -> dict | None:
    m = re.search(r"```json\s*(\{.*?\})\s*```", text, re.DOTALL)
    if not m:
        return None
    try:
        cmd = json.loads(m.group(1))
        if "move" in cmd:
            return cmd
    except json.JSONDecodeError:
        pass
    return None
