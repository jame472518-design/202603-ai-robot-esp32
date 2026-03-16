# Jetson Setup Commands

> Copy and paste these commands on the Jetson terminal, one step at a time.
> All commands assume project is at `~/Desktop/product/202603-ai-robot-esp32/`

## Step 0: SSH Key (run on Jetson, one-time setup)

```bash
mkdir -p ~/.ssh && echo "YOUR_PC_PUBLIC_KEY_HERE" >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys
```

## Step 1: Clone project

```bash
cd ~/Desktop/product && git clone https://github.com/jame472518-design/202603-ai-robot-esp32.git && cd 202603-ai-robot-esp32
```

## Step 2: One-click install (recommended)

```bash
sudo bash scripts/install-jetson.sh
```

This will install all dependencies, build frontend, and configure MQTT.

---

## OR: Manual Step-by-Step Setup

### Step 2a: Install system packages

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients python3-pip python3-venv curl
```

### Step 2b: Fix Mosquitto for external connections

```bash
sudo bash docs/fix-mosquitto.sh
```

### Step 2c: Python environment + dependencies

```bash
cd ~/Desktop/product/202603-ai-robot-esp32 && python3 -m venv venv && source venv/bin/activate && pip install --upgrade pip && pip install -r jetson/requirements.txt && pip install python-multipart
```

### Step 2d: Install Ollama + download model

```bash
curl -fsSL https://ollama.com/install.sh | sh
```

```bash
ollama serve &
```

```bash
sleep 5 && ollama pull qwen2.5:3b
```

### Step 2e: Install Node.js 20 + build frontend

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

```bash
cd ~/Desktop/product/202603-ai-robot-esp32/frontend && npm install && npm run build
```

---

## Start Services

### Option A: One-click start

```bash
bash ~/Desktop/product/202603-ai-robot-esp32/scripts/start.sh
```

### Option B: Manual start (3 terminals)

```bash
# Terminal 1
ollama serve

# Terminal 2
cd ~/Desktop/product/202603-ai-robot-esp32 && source venv/bin/activate && cd jetson && python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 3
cd ~/Desktop/product/202603-ai-robot-esp32/frontend && npx serve dist -l 3000
```

## Stop Services

```bash
bash ~/Desktop/product/202603-ai-robot-esp32/scripts/stop.sh
```

## Check Status

```bash
bash ~/Desktop/product/202603-ai-robot-esp32/scripts/status.sh
```

## Verify

Open browser: `http://JETSON_IP:3000`

API check: `curl http://JETSON_IP:8000/api/status`
