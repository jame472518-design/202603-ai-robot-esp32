#!/bin/bash
# ============================================================
# AI Companion Robot - Check Service Status
# Run: bash scripts/status.sh
# ============================================================

IP=$(hostname -I | awk '{print $1}')

echo "=== AI Companion Robot - Service Status ==="
echo ""

# Mosquitto
printf "%-12s" "Mosquitto:"
systemctl is-active mosquitto 2>/dev/null || echo "not installed"

# Ollama
printf "%-12s" "Ollama:"
if pgrep -x ollama > /dev/null; then echo "running"; else echo "stopped"; fi

# Backend
printf "%-12s" "Backend:"
if pgrep -f "uvicorn app.main:app" > /dev/null; then echo "running (port 8000)"; else echo "stopped"; fi

# Frontend
printf "%-12s" "Frontend:"
if pgrep -f "serve dist" > /dev/null; then echo "running (port 3000)"; else echo "stopped"; fi

echo ""

# API check
if pgrep -f "uvicorn app.main:app" > /dev/null; then
    echo "API Status:"
    curl -s "http://localhost:8000/api/status" 2>/dev/null | python3 -m json.tool 2>/dev/null || echo "  API not responding"
    echo ""
    echo "Devices:"
    curl -s "http://localhost:8000/api/devices" 2>/dev/null | python3 -m json.tool 2>/dev/null || echo "  No devices"
fi

echo ""
echo "Dashboard: http://$IP:3000"
