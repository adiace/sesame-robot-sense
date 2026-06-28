#!/usr/bin/env bash
# Sesame voice service setup — run once on your laptop.
set -e

echo "=== Sesame Voice Service Setup ==="
echo ""

# ── Python deps ───────────────────────────────────────────────────────────────
# Create a virtual environment so we don't touch the system Python
VENV="$( cd "$(dirname "$0")/.." && pwd )/.venv"
if [ ! -d "$VENV" ]; then
    echo "Creating virtual environment at $VENV ..."
    python3 -m venv "$VENV"
fi
source "$VENV/bin/activate"
echo "Installing Python dependencies into venv..."
pip install flask faster-whisper pyttsx3 scipy numpy requests
echo ""

# ── Ollama ────────────────────────────────────────────────────────────────────
if ! command -v ollama &>/dev/null; then
    echo "Ollama is not installed."
    echo ""
    echo "Install it with:"
    echo "  brew install ollama"
    echo ""
    echo "Then re-run this script."
    exit 1
fi

echo "Ollama found: $(ollama --version)"

# Start Ollama server if not already running
if ! curl -s http://localhost:11434 &>/dev/null; then
    echo "Starting Ollama server..."
    ollama serve &>/dev/null &
    sleep 3
fi

if ! ollama list 2>/dev/null | grep -q "llama3.2"; then
    echo "Pulling llama3.2 (~2 GB, one-time download)..."
    ollama pull llama3.2
else
    echo "llama3.2 already present."
fi

# ── Print laptop IP ───────────────────────────────────────────────────────────
IP=$(ipconfig getifaddr en0 2>/dev/null \
  || ipconfig getifaddr en1 2>/dev/null \
  || hostname -I 2>/dev/null | awk '{print $1}' \
  || echo "unknown")

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Setup complete!"
echo ""
echo "Your laptop IP: $IP"
echo ""
echo "Next steps:"
echo "  1. Edit firmware/voice_config.h"
echo "     Set: #define VOICE_SERVER_IP  \"$IP\""
echo ""
echo "  2. Start Ollama (if not already running):"
echo "     ollama serve"
echo ""
echo "  3. Start the voice service:"
echo "     source .venv/bin/activate"
echo "     python software/voice_service.py"
echo ""
echo "  4. Flash the firmware, then hold the button on the robot to talk."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
