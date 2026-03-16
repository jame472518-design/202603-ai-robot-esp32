#!/bin/bash
# ============================================================
# AI Companion Robot - Stop All Services
# Run: bash scripts/stop.sh
# ============================================================

echo "=== Stopping AI Companion Robot Services ==="

# Stop frontend
pkill -f "serve dist" 2>/dev/null && echo "Frontend stopped" || echo "Frontend not running"

# Stop backend
pkill -f "uvicorn app.main:app" 2>/dev/null && echo "Backend stopped" || echo "Backend not running"

# Stop ollama
pkill -x ollama 2>/dev/null && echo "Ollama stopped" || echo "Ollama not running"

echo ""
echo "All services stopped."
echo "MQTT broker (mosquitto) is managed by systemd, still running."
