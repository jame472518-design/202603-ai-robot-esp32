#!/bin/bash
# ============================================================
# AI Companion Robot - Jetson One-Click Install
# Run: sudo bash scripts/install-jetson.sh
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "=== AI Companion Robot - Jetson Installer ==="
echo "Project dir: $PROJECT_DIR"
echo ""

# ------- 1. System Packages -------
echo "[1/6] Installing system packages..."
apt update
apt install -y mosquitto mosquitto-clients python3-pip python3-venv curl

# ------- 2. Mosquitto MQTT Broker -------
echo "[2/6] Configuring Mosquitto MQTT broker..."
cat > /etc/mosquitto/conf.d/external.conf << 'MQTTCONF'
listener 1883 0.0.0.0
allow_anonymous true
MQTTCONF

systemctl enable mosquitto
systemctl restart mosquitto
echo "Mosquitto status: $(systemctl is-active mosquitto)"

# ------- 3. Python Environment -------
echo "[3/6] Setting up Python environment..."
cd "$PROJECT_DIR"
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -r jetson/requirements.txt
pip install python-multipart
deactivate

# ------- 4. Ollama + LLM Model -------
echo "[4/6] Installing Ollama + Qwen2.5:3b..."
if ! command -v ollama &> /dev/null; then
    curl -fsSL https://ollama.com/install.sh | sh
fi
# Start ollama temporarily to pull model
ollama serve &
OLLAMA_PID=$!
sleep 5
ollama pull qwen2.5:3b
kill $OLLAMA_PID 2>/dev/null || true

# ------- 5. Node.js + Frontend Build -------
echo "[5/6] Setting up Node.js and building frontend..."
if ! command -v node &> /dev/null || [ "$(node -v | cut -d. -f1 | tr -d 'v')" -lt 18 ]; then
    echo "Installing Node.js 20..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt install -y nodejs
fi
echo "Node.js version: $(node -v)"

cd "$PROJECT_DIR/frontend"
npm install
npm run build

# ------- 6. Create start script shortcut -------
echo "[6/6] Setting up start script..."
chmod +x "$PROJECT_DIR/scripts/start.sh"
chmod +x "$PROJECT_DIR/scripts/stop.sh"

# ------- Done -------
IP=$(hostname -I | awk '{print $1}')
echo ""
echo "============================================"
echo "  Installation Complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Start services:  bash $PROJECT_DIR/scripts/start.sh"
echo "  2. Open dashboard:  http://$IP:3000"
echo "  3. Flash ESP32 with correct WiFi/MQTT config"
echo ""
echo "ESP32 config to update:"
echo "  WIFI_SSID     = \"YOUR_WIFI\""
echo "  WIFI_PASSWORD = \"YOUR_PASSWORD\""
echo "  MQTT_HOST     = \"$IP\""
echo ""
