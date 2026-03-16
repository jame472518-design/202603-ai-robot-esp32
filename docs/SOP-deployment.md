# AI Companion Robot - Deployment SOP

## Prerequisites

| 设备 | 说明 |
|------|------|
| Jetson Orin 8GB | 已装 JetPack / Ubuntu，连上 WiFi |
| ESP32-S3-CAM (GOOUUU N16R8) | 已接上电脑，Arduino IDE 可烧录 |
| USB 麦克风 | 接 Jetson |
| USB 喇叭 / 3.5mm 音箱 | 接 Jetson |
| 所有设备在同一 WiFi 网络下 | |

---

## Part 1: Jetson 端设置

### 1.1 安装 Mosquitto MQTT Broker

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

验证：
```bash
mosquitto_sub -t "test" &
mosquitto_pub -t "test" -m "hello"
# 看到 hello 即成功
kill %1
```

### 1.2 安装 Python 依赖

```bash
sudo apt install -y python3-pip python3-venv

cd ~/
git clone <your-repo-url> ai-robot
# 或者直接把项目文件夹复制到 Jetson 上
cd ai-robot/jetson

python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn[standard] paho-mqtt websockets ollama numpy
```

### 1.3 安装 Ollama + 下载模型

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama serve &
# 等几秒让 Ollama 启动
ollama pull qwen2.5:7b
```

验证：
```bash
ollama run qwen2.5:7b "你好"
# 应该会回复中文
```

### 1.4 安装 faster-whisper (STT)

```bash
source ~/ai-robot/jetson/venv/bin/activate
pip install faster-whisper
```

注意：Jetson 上可能需要额外安装 CUDA 相关依赖。如果 pip install 失败：
```bash
# 备选方案：用 CPU 模式
# 修改 jetson/app/stt.py，把 device="cuda" 改成 device="cpu"
```

### 1.5 安装 Piper TTS

```bash
pip install piper-tts
# 下载中文语音模型
piper --model zh_CN-huayan-medium --download-dir ~/.local/share/piper-voices
```

如果 piper-tts pip 安装失败，备选方案：
```bash
# 用 Docker
docker run -d -p 5500:5500 rhasspy/piper --voice zh_CN-huayan-medium
```

或修改 `jetson/app/tts.py` 使用 edge-tts（纯网络，不需要本地模型）：
```bash
pip install edge-tts
```

### 1.6 安装 Node.js + 构建前端

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs

cd ~/ai-robot/frontend
npm install
npm run build
```

### 1.7 确认 Jetson IP

```bash
ip addr | grep inet
# 记下 IP 地址（如 10.75.143.199）
```

---

## Part 2: ESP32 端设置

### 2.1 Arduino IDE Board 设置

| 设置 | 值 |
|------|-----|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Port | COM3（或实际端口） |

### 2.2 安装库

Arduino IDE → Sketch → Include Library → Manage Libraries:
- 搜索 **PubSubClient** → 安装 Nick O'Leary 版本

### 2.3 修改配置

打开 `esp32_arduino/esp32_mqtt_node/esp32_mqtt_node.ino`，修改：

```cpp
const char* WIFI_SSID     = "你的WiFi名称";
const char* WIFI_PASSWORD = "你的WiFi密码";
const char* MQTT_HOST     = "Jetson的IP地址";
```

### 2.4 烧录

1. 点击 **Upload** (右箭头按钮)
2. 等待烧录完成
3. 打开 **Serial Monitor** (115200 baud)
4. 确认看到：
   ```
   Connected! IP: x.x.x.x
   Connecting to MQTT... connected!
   Heartbeat sent
   ```

---

## Part 3: 启动服务（每次开机执行）

### 3.1 Jetson 端启动顺序

```bash
# Terminal 1: Ollama
ollama serve

# Terminal 2: Backend
cd ~/ai-robot/jetson
source venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 3: Frontend（开发模式）
cd ~/ai-robot/frontend
npm run dev -- --host 0.0.0.0 --port 3000

# 或者用构建好的静态文件
npx serve dist -l 3000
```

### 3.2 ESP32 端

- 上电即自动运行（已烧录固件）
- 如果换了 WiFi 或 Jetson IP，需要重新烧录

### 3.3 打开 Dashboard

在任意设备浏览器输入：
```
http://JETSON_IP:3000
```

---

## Part 4: 验证清单

| # | 检查项 | 预期结果 |
|---|--------|---------|
| 1 | ESP32 Serial Monitor | 显示 `MQTT... connected!` |
| 2 | 浏览器打开 Dashboard | 右上角显示 `Connected` (绿色) |
| 3 | Device Panel | 显示 esp32s3_cam_001 设备在线 |
| 4 | Sensor Panel | 温度/湿度/光照数值每 2 秒更新 |
| 5 | Chat 输入文字 | LLM 用中文回覆 |
| 6 | 点 Mic 按钮说话 | 语音识别 → LLM 回覆 → 语音播放 |

---

## Part 5: 一键启动脚本（可选）

在 Jetson 上创建 `~/ai-robot/start.sh`：

```bash
#!/bin/bash
echo "=== Starting AI Companion Robot ==="

# Start Ollama in background
ollama serve &
sleep 3

# Start backend
cd ~/ai-robot/jetson
source venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 &
sleep 2

# Start frontend
cd ~/ai-robot/frontend
npx serve dist -l 3000 &

echo "=== All services started ==="
echo "Dashboard: http://$(hostname -I | awk '{print $1}'):3000"
```

```bash
chmod +x ~/ai-robot/start.sh
~/ai-robot/start.sh
```

---

## Troubleshooting

| 问题 | 原因 | 解决 |
|------|------|------|
| ESP32 WiFi 连不上 | SSID/密码错误，或用了 5GHz | 确认 2.4GHz，检查 config |
| MQTT rc=-2 | Jetson 上 Mosquitto 没启动 | `sudo systemctl start mosquitto` |
| MQTT rc=-1 | IP 地址错误 | `ip addr` 确认 Jetson IP |
| Dashboard 显示 Disconnected | Backend 没启动 | 检查 uvicorn 是否在跑 |
| LLM 没回覆 | Ollama 没启动或模型没下载 | `ollama list` 确认模型 |
| 语音没声音 | 喇叭没接或 Piper 没装 | `aplay -l` 检查音频设备 |
| Whisper 报错 | CUDA 不可用 | 改用 `device="cpu"` |
| 换了 WiFi 环境 | IP 地址全变了 | ESP32 改 MQTT_HOST 重烧，重新确认 Jetson IP |
