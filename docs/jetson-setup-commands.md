# Jetson Setup Commands

> Copy and paste these commands on the Jetson terminal, one step at a time.

## Step 0: SSH Key (run this first)

```bash
mkdir -p ~/.ssh && echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIMJahTAfKWFiTgh8ZulhA6Z20tr/nVwmYzeCYQkxHx1s jame4@LAPTOP-4P33R2PM" >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys
```

## Step 1: Clone project

```bash
cd ~ && git clone https://github.com/jame472518-design/202603-ai-robot-esp32.git ai-robot
```

## Step 2: Install system packages

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients python3-pip python3-venv nodejs npm
```

## Step 3: Start Mosquitto

```bash
sudo systemctl enable mosquitto && sudo systemctl start mosquitto
```

## Step 4: Python environment + dependencies

```bash
cd ~/ai-robot/jetson && python3 -m venv venv && source venv/bin/activate && pip install fastapi "uvicorn[standard]" paho-mqtt websockets ollama numpy faster-whisper
```

## Step 5: Install Ollama + download model

```bash
curl -fsSL https://ollama.com/install.sh | sh
```

```bash
ollama serve &
```

```bash
sleep 5 && ollama pull qwen2.5:7b
```

## Step 6: Build frontend

```bash
cd ~/ai-robot/frontend && npm install && npm run build
```

## Step 7: Start backend

```bash
cd ~/ai-robot/jetson && source venv/bin/activate && python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

## Step 8: Start frontend (open another terminal)

```bash
cd ~/ai-robot/frontend && npx serve dist -l 3000
```

## Step 9: Verify

Open browser and go to: `http://JETSON_IP:3000`

---

## Quick restart (after reboot)

```bash
# Terminal 1
ollama serve

# Terminal 2
cd ~/ai-robot/jetson && source venv/bin/activate && python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 3
cd ~/ai-robot/frontend && npx serve dist -l 3000
```
