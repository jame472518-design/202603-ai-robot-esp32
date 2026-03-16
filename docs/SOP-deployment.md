# AI Companion Robot - Deployment SOP

## Prerequisites

| Device | Description |
|--------|-------------|
| Windows PC | Development machine, has Arduino IDE |
| Jetson Orin 8GB | Ubuntu 22.04, connected to same WiFi |
| ESP32-S3-CAM (GOOUUU N16R8) | Connected to PC via USB |
| All devices on the same WiFi network | |

---

## Part 1: Connect PC to Jetson via SSH

### 1.1 Ensure both devices are on the same WiFi

- PC and Jetson must be on the **same WiFi network and subnet**
- Check PC IP: open terminal, run `ipconfig` and look for IPv4 address
- Check Jetson IP: on Jetson terminal, run `hostname -I`
- Both IPs should share the same prefix (e.g., `10.175.143.x`)

### 1.2 Ensure SSH is running on Jetson

On Jetson terminal:
```bash
sudo systemctl status ssh
```

If not running or not installed:
```bash
sudo apt install -y openssh-server
sudo systemctl enable ssh
sudo systemctl start ssh
```

### 1.3 Generate SSH key on PC (one-time setup)

On PC terminal (Git Bash / PowerShell):
```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -N ""
cat ~/.ssh/id_ed25519.pub
```

Copy the output (starts with `ssh-ed25519 ...`).

### 1.4 Add SSH key to Jetson (one-time setup)

On Jetson terminal, paste your public key:
```bash
mkdir -p ~/.ssh
echo "YOUR_PUBLIC_KEY_HERE" >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys
```

### 1.5 Test SSH connection from PC

```bash
ssh aopen@JETSON_IP "echo 'SSH OK' && hostname"
```

Should output:
```
SSH OK
aopen-desktop
```

### 1.6 Troubleshooting SSH

| Problem | Solution |
|---------|----------|
| Connection timed out | Not on same WiFi / subnet |
| Permission denied | SSH key not added correctly, or wrong username |
| Connection refused | SSH service not running on Jetson |
| Different subnet (e.g., 10.75 vs 10.175) | Connect both devices to the same WiFi network |

---

## Part 2: Jetson Setup

### 2.1 Install system packages

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
```

### 2.2 Start Mosquitto MQTT Broker

```bash
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

Verify:
```bash
mosquitto_sub -t "test" &
mosquitto_pub -t "test" -m "hello"
# Should see "hello"
kill %1
```

### 2.3 Clone project

```bash
cd ~/Desktop/product
git clone https://github.com/jame472518-design/202603-ai-robot-esp32.git
cd 202603-ai-robot-esp32
```

### 2.4 Python environment + dependencies

```bash
python3 -m venv venv
source venv/bin/activate
pip install "fastapi[standard]" uvicorn paho-mqtt websockets ollama numpy python-multipart
```

### 2.5 Install Ollama + download model

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama serve &
sleep 5
ollama pull qwen2.5:3b
```

Verify:
```bash
ollama run qwen2.5:3b "你好"
```

### 2.6 Install Node.js + build frontend

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
cd ~/Desktop/product/202603-ai-robot-esp32/frontend
npm install
npm run build
```

---

## Part 3: ESP32 Setup

### 3.1 Arduino IDE Board Settings

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Port | COM3 (or actual port) |

### 3.2 Install Library

Arduino IDE -> Sketch -> Include Library -> Manage Libraries:
- Search **PubSubClient** -> install **Nick O'Leary** version

### 3.3 Update Config

Open `esp32_arduino/esp32_mqtt_node/esp32_mqtt_node.ino`, update:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST     = "JETSON_IP_ADDRESS";
```

### 3.4 Upload

1. Click **Upload** button (right arrow, NOT the bug icon)
2. Wait for upload to complete
3. Open **Serial Monitor** (115200 baud)
4. Confirm output:
   ```
   Connected! IP: x.x.x.x
   Connecting to MQTT... connected!
   Heartbeat sent
   ```

---

## Part 4: Start Services (every boot)

### 4.1 Option A: SSH from PC (recommended)

```bash
# Start Ollama
ssh aopen@JETSON_IP "ollama serve &"

# Start backend
ssh aopen@JETSON_IP "cd ~/Desktop/product/202603-ai-robot-esp32 && source venv/bin/activate && cd jetson && nohup python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 > /tmp/backend.log 2>&1 &"

# Start frontend
ssh aopen@JETSON_IP "cd ~/Desktop/product/202603-ai-robot-esp32/frontend && nohup npx serve dist -l 3000 > /tmp/frontend.log 2>&1 &"
```

### 4.2 Option B: On Jetson directly

```bash
# Terminal 1
ollama serve

# Terminal 2
cd ~/Desktop/product/202603-ai-robot-esp32
source venv/bin/activate
cd jetson
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 3
cd ~/Desktop/product/202603-ai-robot-esp32/frontend
npx serve dist -l 3000
```

### 4.3 ESP32

- Powers on automatically with flashed firmware
- If WiFi or Jetson IP changes, re-flash with updated config

### 4.4 Open Dashboard

Browser: `http://JETSON_IP:3000`

---

## Part 5: Verification Checklist

| # | Check | Expected |
|---|-------|----------|
| 1 | `curl http://JETSON_IP:8000/api/status` | `{"stt":false,"tts":true,"llm":true,"mqtt":true,...}` |
| 2 | ESP32 Serial Monitor | `MQTT... connected!` |
| 3 | Dashboard header | Shows `Connected` (green) |
| 4 | Device Panel | Shows esp32s3_cam_001 online |
| 5 | Sensor Panel | Temperature/humidity/light updating every 2s |
| 6 | Chat input text | LLM responds in Chinese |

---

## Part 6: One-click Start Script (optional)

Create `~/Desktop/product/202603-ai-robot-esp32/start.sh`:

```bash
#!/bin/bash
echo "=== Starting AI Companion Robot ==="

ollama serve &
sleep 3

cd ~/Desktop/product/202603-ai-robot-esp32
source venv/bin/activate
cd jetson
nohup python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 > /tmp/backend.log 2>&1 &
sleep 2

cd ~/Desktop/product/202603-ai-robot-esp32/frontend
nohup npx serve dist -l 3000 > /tmp/frontend.log 2>&1 &

IP=$(hostname -I | awk '{print $1}')
echo "=== All services started ==="
echo "Dashboard: http://$IP:3000"
echo "API: http://$IP:8000/api/status"
```

```bash
chmod +x ~/Desktop/product/202603-ai-robot-esp32/start.sh
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| ESP32 WiFi won't connect | Wrong SSID/password, or using 5GHz | Use 2.4GHz, check config |
| MQTT rc=-2 | Mosquitto not running on Jetson | `sudo systemctl start mosquitto` |
| MQTT rc=-1 | Wrong Jetson IP in ESP32 config | Check `hostname -I` on Jetson, update & re-flash |
| Dashboard shows Disconnected | Backend not running | Check `ps aux \| grep uvicorn` |
| LLM no response | Ollama not running or model not downloaded | `ollama list` to check |
| SSH connection timeout | Different WiFi network / subnet | Connect all devices to same WiFi |
| SSH permission denied | Wrong username or key not added | Re-add SSH key to Jetson |
| `python-multipart` error | Missing dependency | `pip install python-multipart` |
| Changed WiFi | All IPs changed | Update ESP32 MQTT_HOST, re-flash, confirm Jetson IP |
