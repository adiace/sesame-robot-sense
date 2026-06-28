#!/bin/zsh
source ~/.zshrc   # load GROQ_API_KEY and other env vars

cd "$(dirname "$0")"

# Open serial monitor in a separate Terminal window
osascript -e 'tell application "Terminal"
  do script "python3 /Users/adityachaganty/Documents/sesame-robot-sense/software/serial_monitor.py"
end tell'

# Run voice server with auto-restart on crash
echo "Starting Sesame voice server (auto-restart enabled)..."
while true; do
    .venv/bin/python software/voice_service.py
    echo "$(date '+%H:%M:%S') Server exited — restarting in 1s..."
    sleep 1
done
