# AI Companion Robot Demo - Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an AI companion robot demo: ESP32 edge sensors + Jetson 8GB running local LLM/STT/TTS, with a Web Dashboard for client presentation.

**Architecture:** ESP32 devices publish sensor data via MQTT to a Jetson backend (FastAPI + WebSocket). Jetson runs Qwen2.5 7B (Ollama), faster-whisper (STT), Piper (TTS) with sequential model loading. Web Dashboard displays chat UI + sensor data in real-time.

**Tech Stack:** Python 3.10+, FastAPI, paho-mqtt, Ollama, faster-whisper, Piper TTS, Vue 3, PlatformIO, espMqttClient, Mosquitto

---

## Task 1: Project Structure & Git Init

**Files:**
- Create: `README.md`
- Create: `jetson/requirements.txt`
- Create: `jetson/app/__init__.py`
- Create: `esp32/platformio.ini`
- Create: `esp32/src/main.cpp`
- Create: `frontend/package.json`
- Create: `.gitignore`

**Step 1: Initialize project structure**

```
20260308_esp32_wifi_tracker/
├── docs/plans/                  # Already exists
├── jetson/                      # Jetson backend
│   ├── app/
│   │   ├── __init__.py
│   │   ├── main.py              # FastAPI entry
│   │   ├── mqtt_client.py       # MQTT subscriber
│   │   ├── model_scheduler.py   # Load/unload AI models
│   │   ├── stt.py               # Whisper wrapper
│   │   ├── llm.py               # Ollama wrapper
│   │   └── tts.py               # Piper wrapper
│   ├── requirements.txt
│   └── config.py
├── esp32/                       # ESP32 PlatformIO project
│   ├── platformio.ini
│   ├── src/
│   │   └── main.cpp
│   ├── include/
│   │   ├── config.h
│   │   ├── mqtt_handler.h
│   │   └── wifi_manager.h
│   └── lib/
├── frontend/                    # Vue 3 dashboard
│   └── (vite scaffold)
└── .gitignore
```

**Step 2: Create `.gitignore`**

```gitignore
# Python
__pycache__/
*.pyc
.venv/
venv/

# Node
node_modules/
dist/

# PlatformIO
.pio/
.vscode/

# Models
*.bin
*.gguf
*.onnx

# OS
.DS_Store
Thumbs.db

# Environment
.env
```

**Step 3: Create `jetson/requirements.txt`**

```
fastapi==0.115.0
uvicorn[standard]==0.30.0
paho-mqtt==2.1.0
websockets==13.0
ollama==0.4.0
faster-whisper==1.1.0
piper-tts==1.2.0
numpy==1.26.0
```

**Step 4: Create `esp32/platformio.ini`**

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    bertmelis/espMqttClient@^1.7.0

[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps =
    bertmelis/espMqttClient@^1.7.0
```

**Step 5: Create skeleton files and git init**

```bash
git init
git add .
git commit -m "chore: initialize project structure"
```

---

## Task 2: ESP32 MQTT Communication Backbone

**Files:**
- Create: `esp32/include/config.h`
- Create: `esp32/include/wifi_manager.h`
- Create: `esp32/include/mqtt_handler.h`
- Create: `esp32/src/main.cpp`

**Step 1: Create `esp32/include/config.h`**

```cpp
#pragma once

// WiFi
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// MQTT
#define MQTT_HOST "JETSON_IP_ADDRESS"
#define MQTT_PORT 1883
#define DEVICE_ID "esp32_001"

// Topics
#define TOPIC_HEARTBEAT "robot/esp32/" DEVICE_ID "/heartbeat"
#define TOPIC_SENSOR    "robot/esp32/" DEVICE_ID "/sensor"
#define TOPIC_COMMAND   "robot/esp32/" DEVICE_ID "/command"
#define TOPIC_STATUS    "robot/esp32/" DEVICE_ID "/status"

// Intervals (ms)
#define HEARTBEAT_INTERVAL 5000
#define SENSOR_INTERVAL    2000
```

**Step 2: Create `esp32/include/wifi_manager.h`**

```cpp
#pragma once
#include <WiFi.h>
#include "config.h"

void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool wifi_connected() {
    return WiFi.status() == WL_CONNECTED;
}
```

**Step 3: Create `esp32/include/mqtt_handler.h`**

```cpp
#pragma once
#include <espMqttClient.h>
#include "config.h"

espMqttClient mqttClient;
bool mqttConnected = false;

void onMqttConnect(bool sessionPresent) {
    Serial.println("MQTT connected");
    mqttConnected = true;
    mqttClient.subscribe(TOPIC_COMMAND, 1);

    // Publish online status
    mqttClient.publish(TOPIC_STATUS, 1, true, "{\"status\":\"online\"}");
}

void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    Serial.printf("MQTT disconnected, reason: %d\n", (int)reason);
    mqttConnected = false;
}

void onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                   const char* topic, const uint8_t* payload, size_t len,
                   size_t index, size_t total) {
    String msg;
    for (size_t i = 0; i < len; i++) {
        msg += (char)payload[i];
    }
    Serial.printf("Received [%s]: %s\n", topic, msg.c_str());

    // Handle commands from Jetson here
}

void mqtt_init() {
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setClientId(DEVICE_ID);

    // Set last will: if ESP32 disconnects unexpectedly
    mqttClient.setWill(TOPIC_STATUS, 1, true, "{\"status\":\"offline\"}");
}

void mqtt_connect() {
    if (!mqttConnected) {
        Serial.println("Connecting to MQTT...");
        mqttClient.connect();
    }
}

void mqtt_publish_heartbeat() {
    if (!mqttConnected) return;
    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"device\":\"%s\",\"uptime\":%lu,\"heap\":%lu,\"rssi\":%d}",
        DEVICE_ID, millis() / 1000, ESP.getFreeHeap(), WiFi.RSSI());
    mqttClient.publish(TOPIC_HEARTBEAT, 0, false, payload);
}

void mqtt_publish_sensor(const char* json) {
    if (!mqttConnected) return;
    mqttClient.publish(TOPIC_SENSOR, 0, false, json);
}
```

**Step 4: Create `esp32/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"

unsigned long lastHeartbeat = 0;
unsigned long lastSensor = 0;
unsigned long lastReconnect = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== AI Companion Robot - ESP32 Node ===");

    wifi_init();
    mqtt_init();
    mqtt_connect();
}

void loop() {
    mqttClient.loop();

    unsigned long now = millis();

    // Reconnect WiFi if needed
    if (!wifi_connected()) {
        wifi_init();
    }

    // Reconnect MQTT if needed (every 5s)
    if (!mqttConnected && now - lastReconnect > 5000) {
        lastReconnect = now;
        mqtt_connect();
    }

    // Send heartbeat
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        lastHeartbeat = now;
        mqtt_publish_heartbeat();
        Serial.println("Heartbeat sent");
    }

    // Send dummy sensor data (simulated)
    if (now - lastSensor > SENSOR_INTERVAL) {
        lastSensor = now;
        char sensorJson[256];
        float temp = 22.0 + (random(0, 100) / 20.0);   // 22-27°C
        float humidity = 40.0 + (random(0, 100) / 5.0); // 40-60%
        int light = random(200, 800);                     // lux
        snprintf(sensorJson, sizeof(sensorJson),
            "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%d}",
            temp, humidity, light);
        mqtt_publish_sensor(sensorJson);
    }
}
```

**Step 5: Compile and verify**

```bash
cd esp32
pio run -e esp32
```

**Step 6: Commit**

```bash
git add esp32/
git commit -m "feat: ESP32 MQTT communication backbone with heartbeat and dummy sensors"
```

---

## Task 3: Jetson MQTT Receiver + FastAPI Backend

**Files:**
- Create: `jetson/config.py`
- Create: `jetson/app/__init__.py`
- Create: `jetson/app/mqtt_client.py`
- Create: `jetson/app/main.py`

**Step 1: Create `jetson/config.py`**

```python
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC_PREFIX = "robot/esp32/#"

FASTAPI_HOST = "0.0.0.0"
FASTAPI_PORT = 8000

OLLAMA_MODEL = "qwen2.5:7b"
OLLAMA_HOST = "http://localhost:11434"

WHISPER_MODEL = "small"
PIPER_VOICE = "zh_CN-huayan-medium"
```

**Step 2: Create `jetson/app/mqtt_client.py`**

```python
import json
import logging
from collections import defaultdict
from datetime import datetime

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class MQTTManager:
    def __init__(self, broker: str, port: int, topic_prefix: str):
        self.broker = broker
        self.port = port
        self.topic_prefix = topic_prefix
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.devices: dict[str, dict] = {}
        self.sensor_data: dict[str, list] = defaultdict(list)
        self._listeners: list = []

        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        logger.info(f"Connected to MQTT broker, rc={rc}")
        client.subscribe(self.topic_prefix)

    def _on_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            payload = json.loads(msg.payload.decode())
            parts = topic.split("/")
            # topic format: robot/esp32/{device_id}/{data_type}
            if len(parts) >= 4:
                device_id = parts[2]
                data_type = parts[3]

                if data_type == "heartbeat":
                    self.devices[device_id] = {
                        **payload,
                        "last_seen": datetime.now().isoformat(),
                    }
                elif data_type == "sensor":
                    self.sensor_data[device_id].append({
                        **payload,
                        "timestamp": datetime.now().isoformat(),
                    })
                    # Keep last 100 readings
                    self.sensor_data[device_id] = self.sensor_data[device_id][-100:]
                elif data_type == "status":
                    if device_id not in self.devices:
                        self.devices[device_id] = {}
                    self.devices[device_id]["status"] = payload.get("status")

                # Notify WebSocket listeners
                for listener in self._listeners:
                    listener(device_id, data_type, payload)

        except Exception as e:
            logger.error(f"Error processing MQTT message: {e}")

    def add_listener(self, callback):
        self._listeners.append(callback)

    def remove_listener(self, callback):
        self._listeners.remove(callback)

    def start(self):
        self.client.connect(self.broker, self.port)
        self.client.loop_start()
        logger.info(f"MQTT client started, subscribing to {self.topic_prefix}")

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

    def publish(self, topic: str, payload: dict):
        self.client.publish(topic, json.dumps(payload))
```

**Step 3: Create `jetson/app/main.py`**

```python
import asyncio
import json
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from .mqtt_client import MQTTManager
from config import (
    MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX,
    FASTAPI_HOST, FASTAPI_PORT,
)

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

mqtt_manager = MQTTManager(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX)
ws_connections: list[WebSocket] = []


def on_mqtt_data(device_id: str, data_type: str, payload: dict):
    """Forward MQTT data to all WebSocket clients."""
    message = json.dumps({
        "device_id": device_id,
        "type": data_type,
        "data": payload,
    })
    # Schedule async sends from sync callback
    for ws in ws_connections[:]:
        try:
            asyncio.run_coroutine_threadsafe(
                ws.send_text(message),
                asyncio.get_event_loop(),
            )
        except Exception:
            ws_connections.remove(ws)


@asynccontextmanager
async def lifespan(app: FastAPI):
    mqtt_manager.add_listener(on_mqtt_data)
    mqtt_manager.start()
    logger.info("MQTT manager started")
    yield
    mqtt_manager.stop()
    logger.info("MQTT manager stopped")


app = FastAPI(title="AI Companion Robot", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/api/devices")
async def get_devices():
    return mqtt_manager.devices


@app.get("/api/sensors/{device_id}")
async def get_sensor_data(device_id: str):
    return mqtt_manager.sensor_data.get(device_id, [])


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_connections.append(ws)
    logger.info(f"WebSocket client connected, total: {len(ws_connections)}")
    try:
        while True:
            # Keep connection alive, receive messages from frontend
            data = await ws.receive_text()
            message = json.loads(data)
            # Handle chat messages (will be implemented in Task 5)
            if message.get("type") == "chat":
                # Placeholder: echo back
                await ws.send_text(json.dumps({
                    "type": "chat_response",
                    "data": {"text": f"Echo: {message.get('text', '')}"},
                }))
    except WebSocketDisconnect:
        ws_connections.remove(ws)
        logger.info(f"WebSocket client disconnected, total: {len(ws_connections)}")


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=FASTAPI_HOST, port=FASTAPI_PORT)
```

**Step 4: Test locally (without Jetson)**

```bash
cd jetson
pip install -r requirements.txt
# Start mosquitto locally for testing (if available)
# Then:
python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

**Step 5: Commit**

```bash
git add jetson/
git commit -m "feat: Jetson FastAPI backend with MQTT receiver and WebSocket"
```

---

## Task 4: Web Dashboard (Vue 3)

**Files:**
- Create: `frontend/` (Vite + Vue 3 scaffold)
- Create: `frontend/src/App.vue`
- Create: `frontend/src/composables/useWebSocket.js`
- Create: `frontend/src/components/DevicePanel.vue`
- Create: `frontend/src/components/ChatPanel.vue`
- Create: `frontend/src/components/SensorPanel.vue`

**Step 1: Scaffold Vue 3 project**

```bash
cd frontend
npm create vite@latest . -- --template vue
npm install
```

**Step 2: Create `frontend/src/composables/useWebSocket.js`**

```javascript
import { ref, onMounted, onUnmounted } from 'vue'

export function useWebSocket(url) {
  const messages = ref([])
  const devices = ref({})
  const connected = ref(false)
  let ws = null
  let reconnectTimer = null

  function connect() {
    ws = new WebSocket(url)

    ws.onopen = () => {
      connected.value = true
      console.log('WebSocket connected')
    }

    ws.onclose = () => {
      connected.value = false
      console.log('WebSocket disconnected, reconnecting...')
      reconnectTimer = setTimeout(connect, 3000)
    }

    ws.onmessage = (event) => {
      const data = JSON.parse(event.data)
      if (data.type === 'heartbeat') {
        devices.value[data.device_id] = data.data
      } else if (data.type === 'sensor') {
        messages.value.push(data)
        if (messages.value.length > 200) {
          messages.value = messages.value.slice(-200)
        }
      } else if (data.type === 'chat_response') {
        messages.value.push(data)
      }
    }
  }

  function send(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(data))
    }
  }

  onMounted(connect)
  onUnmounted(() => {
    clearTimeout(reconnectTimer)
    if (ws) ws.close()
  })

  return { messages, devices, connected, send }
}
```

**Step 3: Create `frontend/src/components/DevicePanel.vue`**

```vue
<template>
  <div class="device-panel">
    <h3>Device Status</h3>
    <div v-if="Object.keys(devices).length === 0" class="no-devices">
      No devices connected
    </div>
    <div v-for="(info, id) in devices" :key="id" class="device-card">
      <div class="device-header">
        <span class="device-id">{{ id }}</span>
        <span :class="['status-dot', info.status === 'online' ? 'online' : 'offline']"></span>
      </div>
      <div class="device-info">
        <div>Uptime: {{ info.uptime }}s</div>
        <div>Heap: {{ (info.heap / 1024).toFixed(1) }}KB</div>
        <div>RSSI: {{ info.rssi }}dBm</div>
        <div class="last-seen">{{ info.last_seen }}</div>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({ devices: Object })
</script>

<style scoped>
.device-panel { padding: 16px; }
.device-card {
  background: #1e1e2e;
  border-radius: 8px;
  padding: 12px;
  margin-bottom: 8px;
}
.device-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.device-id { font-weight: bold; color: #cdd6f4; }
.status-dot {
  width: 10px; height: 10px;
  border-radius: 50%;
}
.status-dot.online { background: #a6e3a1; }
.status-dot.offline { background: #f38ba8; }
.device-info { font-size: 0.85em; color: #9399b2; margin-top: 8px; }
.no-devices { color: #6c7086; font-style: italic; }
</style>
```

**Step 4: Create `frontend/src/components/ChatPanel.vue`**

```vue
<template>
  <div class="chat-panel">
    <h3>Chat</h3>
    <div class="chat-messages" ref="messagesEl">
      <div v-for="(msg, i) in chatMessages" :key="i"
           :class="['message', msg.role]">
        {{ msg.text }}
      </div>
    </div>
    <div class="chat-input">
      <input v-model="input" @keyup.enter="sendMessage"
             placeholder="Type a message..." />
      <button @click="sendMessage">Send</button>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, nextTick } from 'vue'

const props = defineProps({ messages: Array, send: Function })
const input = ref('')
const chatMessages = ref([])
const messagesEl = ref(null)

function sendMessage() {
  if (!input.value.trim()) return
  chatMessages.value.push({ role: 'user', text: input.value })
  props.send({ type: 'chat', text: input.value })
  input.value = ''
  scrollToBottom()
}

watch(() => props.messages, (msgs) => {
  const last = msgs[msgs.length - 1]
  if (last?.type === 'chat_response') {
    chatMessages.value.push({ role: 'assistant', text: last.data.text })
    scrollToBottom()
  }
}, { deep: true })

function scrollToBottom() {
  nextTick(() => {
    if (messagesEl.value) {
      messagesEl.value.scrollTop = messagesEl.value.scrollHeight
    }
  })
}
</script>

<style scoped>
.chat-panel { padding: 16px; display: flex; flex-direction: column; height: 100%; }
.chat-messages {
  flex: 1; overflow-y: auto;
  padding: 8px; background: #11111b;
  border-radius: 8px; margin-bottom: 8px;
}
.message {
  padding: 8px 12px; border-radius: 12px;
  margin-bottom: 6px; max-width: 80%;
}
.message.user {
  background: #89b4fa; color: #1e1e2e;
  margin-left: auto;
}
.message.assistant {
  background: #313244; color: #cdd6f4;
}
.chat-input { display: flex; gap: 8px; }
.chat-input input {
  flex: 1; padding: 8px 12px;
  background: #1e1e2e; border: 1px solid #45475a;
  color: #cdd6f4; border-radius: 8px; outline: none;
}
.chat-input button {
  padding: 8px 16px; background: #89b4fa;
  color: #1e1e2e; border: none; border-radius: 8px;
  cursor: pointer; font-weight: bold;
}
</style>
```

**Step 5: Create `frontend/src/components/SensorPanel.vue`**

```vue
<template>
  <div class="sensor-panel">
    <h3>Sensor Data</h3>
    <div v-if="latest" class="sensor-grid">
      <div class="sensor-card">
        <div class="sensor-label">Temperature</div>
        <div class="sensor-value">{{ latest.temperature?.toFixed(1) }} °C</div>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Humidity</div>
        <div class="sensor-value">{{ latest.humidity?.toFixed(1) }} %</div>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Light</div>
        <div class="sensor-value">{{ latest.light }} lux</div>
      </div>
    </div>
    <div v-else class="no-data">Waiting for sensor data...</div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({ messages: Array })

const latest = computed(() => {
  const sensorMsgs = props.messages.filter(m => m.type === 'sensor')
  if (sensorMsgs.length === 0) return null
  return sensorMsgs[sensorMsgs.length - 1].data
})
</script>

<style scoped>
.sensor-panel { padding: 16px; }
.sensor-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
.sensor-card {
  background: #1e1e2e; border-radius: 8px;
  padding: 16px; text-align: center;
}
.sensor-label { color: #9399b2; font-size: 0.85em; margin-bottom: 4px; }
.sensor-value { color: #cdd6f4; font-size: 1.5em; font-weight: bold; }
.no-data { color: #6c7086; font-style: italic; }
</style>
```

**Step 6: Create `frontend/src/App.vue`**

```vue
<template>
  <div class="app">
    <header>
      <h1>AI Companion Robot</h1>
      <span :class="['conn-status', connected ? 'on' : 'off']">
        {{ connected ? 'Connected' : 'Disconnected' }}
      </span>
    </header>
    <main>
      <aside>
        <DevicePanel :devices="devices" />
        <SensorPanel :messages="messages" />
      </aside>
      <section class="chat-section">
        <ChatPanel :messages="messages" :send="send" />
      </section>
    </main>
  </div>
</template>

<script setup>
import DevicePanel from './components/DevicePanel.vue'
import ChatPanel from './components/ChatPanel.vue'
import SensorPanel from './components/SensorPanel.vue'
import { useWebSocket } from './composables/useWebSocket'

const wsUrl = `ws://${window.location.hostname}:8000/ws`
const { messages, devices, connected, send } = useWebSocket(wsUrl)
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { background: #181825; color: #cdd6f4; font-family: system-ui, sans-serif; }
.app { height: 100vh; display: flex; flex-direction: column; }
header {
  display: flex; justify-content: space-between; align-items: center;
  padding: 12px 24px; background: #1e1e2e; border-bottom: 1px solid #313244;
}
header h1 { font-size: 1.2em; }
.conn-status {
  padding: 4px 12px; border-radius: 12px; font-size: 0.8em;
}
.conn-status.on { background: #a6e3a1; color: #1e1e2e; }
.conn-status.off { background: #f38ba8; color: #1e1e2e; }
main {
  flex: 1; display: flex; overflow: hidden;
}
aside {
  width: 350px; border-right: 1px solid #313244;
  overflow-y: auto;
}
.chat-section { flex: 1; }
</style>
```

**Step 7: Test frontend dev server**

```bash
cd frontend
npm run dev
```

**Step 8: Commit**

```bash
git add frontend/
git commit -m "feat: Vue 3 Web Dashboard with device, sensor, and chat panels"
```

---

## Task 5: LLM Integration (Ollama + Qwen2.5)

**Files:**
- Create: `jetson/app/llm.py`
- Modify: `jetson/app/main.py` (wire up chat endpoint)

**Step 1: Create `jetson/app/llm.py`**

```python
import logging
from ollama import Client

logger = logging.getLogger(__name__)

SYSTEM_PROMPT = """你是一个友善的AI陪伴机器人助手。你能感知环境数据（温度、湿度、光照等），
并基于这些数据与用户自然对话。回答简洁、有温度、像朋友一样。用中文回答。"""


class LLMService:
    def __init__(self, model: str, host: str):
        self.model = model
        self.client = Client(host=host)
        self.history: list[dict] = []

    def chat(self, user_message: str, sensor_context: str = "") -> str:
        if sensor_context:
            context_msg = f"[当前环境数据: {sensor_context}]\n{user_message}"
        else:
            context_msg = user_message

        self.history.append({"role": "user", "content": context_msg})

        # Keep history manageable
        messages = [{"role": "system", "content": SYSTEM_PROMPT}]
        messages += self.history[-10:]  # Last 10 turns

        try:
            response = self.client.chat(
                model=self.model,
                messages=messages,
                stream=False,
            )
            reply = response["message"]["content"]
            self.history.append({"role": "assistant", "content": reply})
            return reply
        except Exception as e:
            logger.error(f"LLM error: {e}")
            return f"抱歉，我暂时无法回答。({e})"

    def clear_history(self):
        self.history.clear()
```

**Step 2: Modify `jetson/app/main.py` — replace chat echo with LLM call**

In the WebSocket handler, replace the echo placeholder:

```python
# Add import at top
from .llm import LLMService
from config import OLLAMA_MODEL, OLLAMA_HOST

# Initialize after mqtt_manager
llm_service = LLMService(OLLAMA_MODEL, OLLAMA_HOST)

# In websocket_endpoint, replace the echo block:
if message.get("type") == "chat":
    text = message.get("text", "")
    # Get latest sensor context
    sensor_ctx = ""
    for device_data in mqtt_manager.sensor_data.values():
        if device_data:
            latest = device_data[-1]
            sensor_ctx = f"温度:{latest.get('temperature')}°C, 湿度:{latest.get('humidity')}%, 光照:{latest.get('light')}lux"
            break
    reply = llm_service.chat(text, sensor_ctx)
    await ws.send_text(json.dumps({
        "type": "chat_response",
        "data": {"text": reply},
    }))
```

**Step 3: Test with Ollama running**

```bash
# On Jetson (or local with Ollama installed):
ollama pull qwen2.5:7b
python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
# Open browser, type in chat, verify LLM responds
```

**Step 4: Commit**

```bash
git add jetson/app/llm.py jetson/app/main.py
git commit -m "feat: integrate Ollama Qwen2.5 7B for chat with sensor context"
```

---

## Task 6: STT Service (faster-whisper)

**Files:**
- Create: `jetson/app/stt.py`
- Modify: `jetson/app/main.py` (add audio upload endpoint)

**Step 1: Create `jetson/app/stt.py`**

```python
import io
import logging
import numpy as np
from faster_whisper import WhisperModel

logger = logging.getLogger(__name__)


class STTService:
    def __init__(self, model_size: str = "small"):
        self.model_size = model_size
        self.model = None

    def load(self):
        if self.model is None:
            logger.info(f"Loading Whisper {self.model_size}...")
            self.model = WhisperModel(
                self.model_size,
                device="cuda",
                compute_type="float16",
            )
            logger.info("Whisper loaded")

    def unload(self):
        if self.model is not None:
            del self.model
            self.model = None
            logger.info("Whisper unloaded")

    def transcribe(self, audio_bytes: bytes) -> str:
        self.load()
        audio_array = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0
        segments, info = self.model.transcribe(
            audio_array,
            language="zh",
            beam_size=3,
            vad_filter=True,
        )
        text = "".join(segment.text for segment in segments)
        logger.info(f"Transcribed: {text}")
        return text.strip()
```

**Step 2: Add audio endpoint to `jetson/app/main.py`**

```python
from fastapi import UploadFile, File
from .stt import STTService
from config import WHISPER_MODEL

stt_service = STTService(WHISPER_MODEL)

@app.post("/api/transcribe")
async def transcribe_audio(file: UploadFile = File(...)):
    audio_bytes = await file.read()
    text = stt_service.transcribe(audio_bytes)
    return {"text": text}
```

**Step 3: Commit**

```bash
git add jetson/app/stt.py jetson/app/main.py
git commit -m "feat: add STT service with faster-whisper"
```

---

## Task 7: TTS Service (Piper)

**Files:**
- Create: `jetson/app/tts.py`
- Modify: `jetson/app/main.py` (add TTS endpoint)

**Step 1: Create `jetson/app/tts.py`**

```python
import io
import logging
import subprocess
import wave

logger = logging.getLogger(__name__)


class TTSService:
    def __init__(self, voice: str = "zh_CN-huayan-medium"):
        self.voice = voice

    def synthesize(self, text: str) -> bytes:
        """Use piper CLI to synthesize speech, return WAV bytes."""
        try:
            result = subprocess.run(
                ["piper", "--model", self.voice, "--output-raw"],
                input=text.encode("utf-8"),
                capture_output=True,
                timeout=30,
            )
            if result.returncode != 0:
                logger.error(f"Piper error: {result.stderr.decode()}")
                return b""

            # Wrap raw PCM in WAV header (16kHz, 16-bit, mono)
            raw_audio = result.stdout
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(22050)
                wf.writeframes(raw_audio)
            return buf.getvalue()
        except Exception as e:
            logger.error(f"TTS error: {e}")
            return b""
```

**Step 2: Add TTS endpoint to `jetson/app/main.py`**

```python
from fastapi.responses import Response
from .tts import TTSService
from config import PIPER_VOICE

tts_service = TTSService(PIPER_VOICE)

@app.post("/api/tts")
async def text_to_speech(text: str):
    audio = tts_service.synthesize(text)
    return Response(content=audio, media_type="audio/wav")
```

**Step 3: Commit**

```bash
git add jetson/app/tts.py jetson/app/main.py
git commit -m "feat: add TTS service with Piper"
```

---

## Task 8: Voice Pipeline Integration

**Files:**
- Modify: `jetson/app/main.py` (full voice pipeline via WebSocket)
- Modify: `frontend/src/components/ChatPanel.vue` (add mic recording)

**Step 1: Add voice pipeline to backend**

Add to `jetson/app/main.py`:

```python
from .model_scheduler import ModelScheduler

scheduler = ModelScheduler(stt_service, llm_service, tts_service)

@app.post("/api/voice")
async def voice_pipeline(file: UploadFile = File(...)):
    """Full pipeline: audio in → text → LLM → TTS → audio out"""
    audio_bytes = await file.read()

    # STT
    text = stt_service.transcribe(audio_bytes)

    # LLM
    sensor_ctx = ""
    for device_data in mqtt_manager.sensor_data.values():
        if device_data:
            latest = device_data[-1]
            sensor_ctx = f"温度:{latest.get('temperature')}°C, 湿度:{latest.get('humidity')}%, 光照:{latest.get('light')}lux"
            break
    reply = llm_service.chat(text, sensor_ctx)

    # TTS
    audio_out = tts_service.synthesize(reply)

    return Response(content=audio_out, media_type="audio/wav",
                    headers={"X-Input-Text": text, "X-Reply-Text": reply})
```

**Step 2: Create `jetson/app/model_scheduler.py`**

```python
import logging
import gc
import torch

logger = logging.getLogger(__name__)


class ModelScheduler:
    """Manages model loading/unloading to fit in 8GB RAM."""

    def __init__(self, stt, llm, tts):
        self.stt = stt
        self.llm = llm
        self.tts = tts

    def clear_gpu(self):
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
        logger.info("GPU memory cleared")
```

**Step 3: Add mic recording to `ChatPanel.vue`**

```vue
<!-- Add to template, inside chat-input div -->
<button @click="toggleRecording" :class="{ recording: isRecording }">
  {{ isRecording ? '⏹ Stop' : '🎤 Mic' }}
</button>
```

```javascript
// Add to script setup
const isRecording = ref(false)
let mediaRecorder = null
let audioChunks = []

async function toggleRecording() {
  if (isRecording.value) {
    mediaRecorder.stop()
    isRecording.value = false
  } else {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true })
    mediaRecorder = new MediaRecorder(stream)
    audioChunks = []
    mediaRecorder.ondataavailable = (e) => audioChunks.push(e.data)
    mediaRecorder.onstop = async () => {
      const blob = new Blob(audioChunks, { type: 'audio/webm' })
      const formData = new FormData()
      formData.append('file', blob, 'recording.webm')

      chatMessages.value.push({ role: 'user', text: '🎤 (speaking...)' })
      const res = await fetch('/api/voice', { method: 'POST', body: formData })
      const inputText = res.headers.get('X-Input-Text')
      const replyText = res.headers.get('X-Reply-Text')

      // Update user message with transcription
      chatMessages.value[chatMessages.value.length - 1].text = inputText || '(no speech detected)'
      chatMessages.value.push({ role: 'assistant', text: replyText })

      // Play audio response
      const audioBlob = await res.blob()
      const audioUrl = URL.createObjectURL(audioBlob)
      new Audio(audioUrl).play()

      stream.getTracks().forEach(t => t.stop())
    }
    mediaRecorder.start()
    isRecording.value = true
  }
}
```

**Step 4: Commit**

```bash
git add jetson/ frontend/
git commit -m "feat: integrate full voice pipeline (STT → LLM → TTS) with mic recording"
```

---

## Task 9: Integration Test & Polish

**Step 1: Start all services on Jetson**

```bash
# Terminal 1: Mosquitto
sudo systemctl start mosquitto

# Terminal 2: Ollama
ollama serve
ollama pull qwen2.5:7b

# Terminal 3: Backend
cd jetson
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 4: Frontend
cd frontend
npm run build
# Serve built files from FastAPI or:
npx serve dist -l 3000
```

**Step 2: Flash ESP32**

```bash
cd esp32
# Edit include/config.h with actual WiFi and Jetson IP
pio run -e esp32 -t upload
pio device monitor
```

**Step 3: Verify end-to-end**

- [ ] ESP32 heartbeat appears on Dashboard
- [ ] Sensor data updates in real-time
- [ ] Text chat works with Qwen2.5
- [ ] LLM references sensor data in responses
- [ ] Voice input → transcription → response → audio playback

**Step 4: Commit**

```bash
git add .
git commit -m "chore: integration test and polish"
```

---

**Plan complete and saved to `docs/plans/2026-03-10-implementation-plan.md`。两种执行方式：**

**1. Subagent-Driven（本 session）** — 我逐个 task 派发 subagent 执行，每个 task 之间做 review，快速迭代

**2. Parallel Session（另开 session）** — 另开新 session 用 executing-plans 批量执行，有 checkpoint 检查

**你选哪种？**