"""
Text-to-Speech using macOS `say` command + ffmpeg to produce WAV.

Produkuje WAV 16kHz, 16-bit, mono — dokładnie taki format jakiego oczekuje
ESP32 audio_player.cpp (I2S skonfigurowane na 16kHz/16-bit/mono).
"""

import subprocess
import tempfile
import os
import logging

logger = logging.getLogger(__name__)

# macOS `say` voice for Polish — Zosia is the built-in Polish voice
# Run `say -v '?'` to list all voices
VOICE = "Zosia"  # Polish female voice; fallback: "Paulina"

# Format audio dopasowany do konfiguracji I2S w audio_player.cpp
WAV_SAMPLE_RATE = 16000  # Hz — musi zgadzać się z I2S begin() w firmware
WAV_CHANNELS = 1         # mono
WAV_BIT_DEPTH = "s16le"  # signed 16-bit little-endian PCM


def text_to_wav(text: str) -> bytes:
    """
    Convert text to WAV audio bytes using macOS `say` + ffmpeg.
    Output: WAV RIFF, 16kHz, 16-bit, mono.
    Returns WAV bytes or empty bytes on failure.
    """
    try:
        with tempfile.TemporaryDirectory() as tmpdir:
            aiff_path = os.path.join(tmpdir, "out.aiff")
            wav_path = os.path.join(tmpdir, "out.wav")

            # Generate AIFF with macOS say
            say_result = subprocess.run(
                ["say", "-v", VOICE, "-o", aiff_path, text],
                capture_output=True,
                timeout=30,
            )

            if say_result.returncode != 0:
                logger.warning(f"[TTS] Voice '{VOICE}' failed, trying default voice")
                say_result = subprocess.run(
                    ["say", "-o", aiff_path, text],
                    capture_output=True,
                    timeout=30,
                )

            if say_result.returncode != 0:
                raise RuntimeError(f"say failed: {say_result.stderr.decode()}")

            # Convert AIFF -> WAV 16kHz/16-bit/mono with ffmpeg
            ffmpeg_result = subprocess.run(
                [
                    "ffmpeg",
                    "-y",
                    "-i", aiff_path,
                    "-ar", str(WAV_SAMPLE_RATE),  # resample to 16kHz
                    "-ac", str(WAV_CHANNELS),      # mono
                    "-c:a", "pcm_s16le",           # 16-bit signed PCM
                    wav_path,
                ],
                capture_output=True,
                timeout=30,
            )

            if ffmpeg_result.returncode != 0:
                raise RuntimeError(f"ffmpeg failed: {ffmpeg_result.stderr.decode()[-200:]}")

            with open(wav_path, "rb") as f:
                wav_bytes = f.read()

            logger.info(f"[TTS] Generated {len(wav_bytes)} bytes WAV ({WAV_SAMPLE_RATE}Hz/16bit/mono) for: {text[:50]!r}")
            return wav_bytes

    except Exception as e:
        logger.error(f"[TTS] Failed: {e}")
        return b""


# Zachowaj stary alias dla lokalnego odtwarzania (afplay obsługuje WAV)
def text_to_mp3(text: str) -> bytes:
    """Alias zachowany dla kompatybilności — teraz zwraca WAV zamiast MP3."""
    return text_to_wav(text)


def list_polish_voices() -> list[str]:
    """Return list of available voices containing 'pl' or common Polish names."""
    result = subprocess.run(["say", "-v", "?"], capture_output=True, text=True)
    voices = []
    for line in result.stdout.splitlines():
        low = line.lower()
        if "pl_" in low or "zosia" in low or "paulina" in low or "polish" in low:
            voices.append(line.strip())
    return voices
