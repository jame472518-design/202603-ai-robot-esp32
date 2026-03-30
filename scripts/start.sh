#!/bin/bash
# ============================================================
# AI Companion Robot - Start All Services
# Run: bash scripts/start.sh
# ============================================================

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IP=$(hostname -I | awk '{print $1}')

# Load nvm if available (needed for Node.js 20)
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"

echo "=== AI Companion Robot - Starting Services ==="
echo ""

# ------- 1. Ollama -------
if pgrep -x ollama > /dev/null; then
    echo "[1/3] Ollama already running"
else
    echo "[1/3] Starting Ollama..."
    ollama serve > /tmp/ollama.log 2>&1 &
    sleep 3
fi

# ------- 2. Backend (FastAPI) -------
if pgrep -f "uvicorn app.main:app" > /dev/null; then
    echo "[2/3] Backend already running"
else
    echo "[2/3] Starting Backend..."
    cd "$PROJECT_DIR"
    source venv/bin/activate
    cd jetson
    nohup python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 > /tmp/backend.log 2>&1 &
    sleep 2
fi

# ------- 3. Frontend -------
if pgrep -f "serve dist" > /dev/null; then
    echo "[3/3] Frontend already running"
else
    echo "[3/3] Starting Frontend..."
    cd "$PROJECT_DIR/frontend"
    nohup npx serve dist -l 3000 > /tmp/frontend.log 2>&1 &
    sleep 2
fi

echo ""
echo "============================================"
echo "  All Services Started!"
echo "============================================"
echo ""
echo "  Dashboard:  http://$IP:3000"
echo "  API:        http://$IP:8000/api/status"
echo "  MQTT:       $IP:1883"
echo ""
echo "Logs:"
echo "  Backend:  tail -f /tmp/backend.log"
echo "  Frontend: tail -f /tmp/frontend.log"
echo "  Ollama:   tail -f /tmp/ollama.log"
echo ""
