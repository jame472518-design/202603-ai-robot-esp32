# AI Companion Robot Demo

ESP32-S3-CAM 边缘传感器 + Jetson Orin 8GB 本地 AI 机器人展示系统。

## 系统架构

```
ESP32-S3-CAM ──MQTT──► Mosquitto Broker ──► Jetson Backend ──WebSocket──► Web Dashboard
   (传感器/摄像头)         (Jetson)          (FastAPI + AI)               (Vue 3)
```

| 组件 | 说明 |
|------|------|
| `esp32_arduino/` | ESP32-S3-CAM Arduino 固件 (MQTT + MJPEG 摄像头串流) |
| `esp32/` | ESP32 PlatformIO 项目 (备用) |
| `jetson/` | Jetson Python 后端 (FastAPI, LLM, STT, TTS, MQTT) |
| `frontend/` | Vue 3 Web Dashboard (实时数据 + 摄像头 + 聊天) |
| `docs/` | 部署 SOP、设计文档、安装脚本 |

## 硬件需求

| 设备 | 型号 | 用途 |
|------|------|------|
| Jetson | Orin 8GB, Ubuntu 22.04 | AI 运算 + MQTT Broker + Web Server |
| ESP32 | GOOUUU ESP32-S3-CAM N16R8 (OV2640) | 边缘传感器 + 摄像头 |
| Windows PC | 任意 | 开发机，Arduino IDE 烧录 |
| WiFi | 2.4GHz (ESP32 不支持 5GHz) | 所有设备需在同一网络 |

## AI 模型

| 功能 | 模型 | 备注 |
|------|------|------|
| LLM | Qwen2.5:3b (Ollama) | 中文对话，8GB RAM 适配 |
| STT | faster-whisper small | 语音转文字 (可选) |
| TTS | Piper zh_CN-huayan-medium | 文字转语音 (可选) |

## 快速开始

### 1. Jetson 一键安装

```bash
# 在 Jetson 上执行
cd ~/Desktop/product
git clone https://github.com/jame472518-design/202603-ai-robot-esp32.git
cd 202603-ai-robot-esp32
sudo bash scripts/install-jetson.sh
```

### 2. ESP32 烧录

用 Arduino IDE 打开 `esp32_arduino/esp32_mqtt_node/esp32_mqtt_node.ino`，修改 WiFi 和 MQTT 配置后上传。

详细设置见 [SOP-deployment.md](docs/SOP-deployment.md#part-3-esp32-setup)

### 3. 启动服务

```bash
# 在 Jetson 上执行
bash scripts/start.sh
```

### 4. 打开 Dashboard

浏览器访问: `http://JETSON_IP:3000`

## 文档

- [部署 SOP](docs/SOP-deployment.md) - 完整部署步骤
- [Jetson 快速命令](docs/jetson-setup-commands.md) - 可复制粘贴的命令
- [系统设计](docs/plans/2026-03-10-ai-companion-robot-design.md) - 架构设计文档
- [实现计划](docs/plans/2026-03-10-implementation-plan.md) - 详细任务计划

## Dashboard 功能

| 面板 | 功能 |
|------|------|
| Device Panel | ESP32 设备在线状态、信号强度、运行时间 |
| Sensor Panel | 温度/湿度/光照 实时图表 |
| Camera Panel | ESP32-S3-CAM MJPEG 实时画面 |
| Chat Panel | LLM 中文对话 (可结合传感器数据) |
