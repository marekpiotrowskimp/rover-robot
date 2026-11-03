"""
Speech-to-Text using faster-whisper (local, runs on CPU/GPU).
Model is downloaded once and cached by HuggingFace hub.
Uses the 'tiny' model for speed on CPU; switch to 'base' or 'small' for better accuracy.
"""

import io
import wave
import logging
import tempfile
import os

logger = logging.getLogger(__name__)

_model = None
MODEL_SIZE = "small"  # tiny | base | small | medium — trade-off speed vs accuracy


def _get_model():
    global _model
    if _model is None:
        logger.info(f"[STT] Loading faster-whisper model '{MODEL_SIZE}'...")
        from faster_whisper import WhisperModel
        _model = WhisperModel(MODEL_SIZE, device="cpu", compute_type="int8")
        logger.info("[STT] Model ready")
    return _model


def transcribe_wav(wav_bytes: bytes, language: str = "pl") -> str:
    """
    Transcribe raw WAV bytes using faster-whisper.
    Returns stripped transcript text or '' on failure.
    """
    try:
        model = _get_model()

        # Write to temp file (faster-whisper needs a file path)
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp.write(wav_bytes)
            tmp_path = tmp.name

        try:
            segments, info = model.transcribe(
                tmp_path,
                language=language,
                beam_size=5,
                vad_filter=True,         # filter out silence
                vad_parameters={"min_silence_duration_ms": 500},
            )
            text = " ".join(seg.text for seg in segments).strip()
        finally:
            os.unlink(tmp_path)

        logger.info(f"[STT] Transcribed ({info.language}): {text!r}")
        return text

    except Exception as e:
        logger.error(f"[STT] Transcription failed: {e}")
        return ""
