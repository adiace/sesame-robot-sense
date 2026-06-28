#!/usr/bin/env python3
"""
Sesame voice service.

Receives raw mono 16-bit 16kHz PCM from the ESP32, runs:
  faster-whisper        (speech → text, local)
  Groq llama-3.3-70b   (text → text, cloud ~200ms) with Ollama fallback
  macOS say+afconvert   (text → WAV)

Returns a WAV file the ESP32 plays through the MAX98357A speaker.

Setup (once):
  pip install groq
  export GROQ_API_KEY=gsk_...   # from console.groq.com (free tier)

Run:
  python software/voice_service.py

If GROQ_API_KEY is unset or Groq fails (rate-limit, network), falls back
to local Ollama automatically.
"""

import io, os, socket, subprocess, tempfile, logging
import numpy as np
from flask import Flask, request, send_file
from faster_whisper import WhisperModel
import requests as req_lib

logging.basicConfig(level=logging.INFO, format="%(asctime)s  %(message)s")
log = logging.getLogger(__name__)

# ── Config ────────────────────────────────────────────────────────────────────
PORT          = 5005
WHISPER_MODEL = "tiny"          # "tiny" = fast; "base" = better accuracy
GROQ_MODEL    = "llama-3.3-70b-versatile"   # fast + smart on Groq hardware
OLLAMA_MODEL  = "llama3.2"      # local fallback
OLLAMA_URL    = "http://localhost:11434/api/chat"
SYSTEM_PROMPT = (
    "You are Sesame, a friendly little quadruped robot. "
    "Keep every reply to 1-2 short sentences. Be warm and playful."
)
PCM_RATE      = 16000
WAV_OUT_RATE  = 16000

# ── Groq client (optional — graceful if not installed / no key) ───────────────
_groq = None
try:
    from groq import Groq
    _groq_key = os.environ.get("GROQ_API_KEY", "")
    if _groq_key:
        _groq = Groq(api_key=_groq_key)
        log.info("Groq ready  (model=%s)", GROQ_MODEL)
    else:
        log.info("GROQ_API_KEY not set — using Ollama only")
except ImportError:
    log.info("groq package not installed — using Ollama only  (pip install groq)")

# ── Load Whisper once at startup ──────────────────────────────────────────────
log.info("Loading Whisper '%s' ...", WHISPER_MODEL)
_whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
log.info("Whisper ready.")

app = Flask(__name__)

# ── Helpers ───────────────────────────────────────────────────────────────────

def _pcm_to_float(raw: bytes) -> np.ndarray:
    return np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0


def _tts(text: str) -> bytes:
    """Text → mono 16-bit 16kHz WAV via macOS say + afconvert."""
    aiff_path = wav_path = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as f:
            aiff_path = f.name
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            wav_path = f.name
        subprocess.run(["say", "-r", "165", "-o", aiff_path, text],
                       check=True, capture_output=True)
        subprocess.run(
            ["afconvert", "-f", "WAVE", "-d", f"LEI16@{WAV_OUT_RATE}", aiff_path, wav_path],
            check=True, capture_output=True
        )
        with open(wav_path, "rb") as f:
            return f.read()
    finally:
        for p in [aiff_path, wav_path]:
            if p:
                try: os.unlink(p)
                except OSError: pass


def _llm_groq(text: str) -> str:
    """Groq cloud inference — fast (~200ms), free tier 14 400 req/day."""
    completion = _groq.chat.completions.create(
        model=GROQ_MODEL,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": text},
        ],
        max_tokens=120,
        temperature=0.7,
    )
    return completion.choices[0].message.content.strip()


def _llm_ollama(text: str) -> str:
    """Local Ollama fallback — no rate limit, ~900ms on Apple Silicon."""
    r = req_lib.post(OLLAMA_URL, json={
        "model": OLLAMA_MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": text},
        ],
        "stream": False,
    }, timeout=30)
    r.raise_for_status()
    return r.json()["message"]["content"].strip()


def _llm(text: str) -> str:
    """Try Groq first; fall back to Ollama on any failure."""
    if _groq:
        try:
            reply = _llm_groq(text)
            log.info("LLM (Groq): %r", reply)
            return reply
        except Exception as e:
            log.warning("Groq failed (%s) — falling back to Ollama", e)
    try:
        reply = _llm_ollama(text)
        log.info("LLM (Ollama): %r", reply)
        return reply
    except Exception as e:
        log.error("Ollama error: %s", e)
        return "Sorry, I had trouble thinking about that."


# ── Route ─────────────────────────────────────────────────────────────────────

@app.route("/listen", methods=["POST"])
def listen():
    pcm = request.data
    log.info("Received %d bytes of PCM (%.1fs)", len(pcm), len(pcm) / (PCM_RATE * 2))

    if len(pcm) < PCM_RATE * 2 * 0.3:
        log.info("Too short, ignoring.")
        return send_file(io.BytesIO(_tts("I didn't catch that.")), mimetype="audio/wav")

    audio = _pcm_to_float(pcm)
    segments, _ = _whisper.transcribe(audio, language="en", vad_filter=True)
    text = " ".join(s.text for s in segments).strip()
    log.info("Heard: %r", text)

    if not text:
        return send_file(io.BytesIO(_tts("I didn't catch that, try again.")), mimetype="audio/wav")

    reply = _llm(text)
    wav = _tts(reply)
    return send_file(io.BytesIO(wav), mimetype="audio/wav")


# ── Main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
    except Exception:
        ip = "unknown"
    log.info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    log.info("Voice service  port=%d  llm=%s",
             PORT, f"Groq({GROQ_MODEL})+Ollama fallback" if _groq else f"Ollama({OLLAMA_MODEL})")
    log.info("Laptop IP: %s  →  set VOICE_SERVER_IP in firmware/voice_config.h", ip)
    log.info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    app.run(host="0.0.0.0", port=PORT, debug=False, threaded=True)
