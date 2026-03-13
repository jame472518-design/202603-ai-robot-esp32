# AI Companion Robot Demo - Design Document

## Overview

A companion robot demo system using ESP32 devices as edge sensors and Jetson Orin 8GB as the central AI compute platform. Phase C (software-first, no physical body) then Phase B (mobile robot).

## Target Audience

External clients interested in smart home and industrial applications.

## Architecture

```
┌──────────────────────────────────────────────────┐
│                   Jetson 8GB                      │
│                                                   │
│  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ Whisper  │  │ Qwen2 7B │  │  TTS (VITS)    │  │
│  │  (STT)   │  │ (4-bit)  │  │                │  │
│  └────┬─────┘  └────┬─────┘  └──────┬─────────┘  │
│       │              │               │            │
│  ┌────▼──────────────▼───────────────▼─────────┐  │
│  │          Python Backend (FastAPI)            │  │
│  │  - MQTT Client                              │  │
│  │  - Model Scheduler (load/unload by stage)   │  │
│  │  - WebSocket realtime push                  │  │
│  └────────────────┬────────────────────────────┘  │
│                   │                               │
│  ┌────────────────▼────────────────────────────┐  │
│  │        Web Dashboard (Vue or React)          │  │
│  │  - Chat UI + simple animation               │  │
│  │  - Sensor data panel                        │  │
│  │  - Audio record/playback                    │  │
│  └─────────────────────────────────────────────┘  │
└──────────────────────┬────────────────────────────┘
                       │ MQTT
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   [ESP32 #1]    [ESP32-S3-CAM]   [ESP32 #3]
   Comm backbone   Image capture   Mic array
   (Phase 1)      (Phase 2)       (Phase 3)

External: Mosquitto MQTT Broker runs on Jetson
```

## Development Phases

### Phase 1: Communication Backbone
- ESP32 connects to Jetson via WiFi
- MQTT broker (Mosquitto) on Jetson
- ESP32 publishes heartbeat + dummy sensor data
- Jetson receives and displays on Web Dashboard
- Validate round-trip latency

### Phase 2: Voice Conversation Pipeline
- Microphone input (USB mic on Jetson initially)
- Whisper small for STT
- Qwen2 7B 4-bit for response generation
- VITS/Piper for TTS
- Speaker output
- Model scheduler: load/unload models sequentially to fit 8GB

### Phase 3: Memory Management
- Sequential pipeline: Record → STT → unload → LLM → unload → TTS → play
- Peak memory ~4.5GB (Qwen2 4-bit)
- Monitor and optimize loading/unloading time

### Phase 4: ESP32-S3-CAM Integration
- ESP32-S3-CAM captures images, sends via MQTT or HTTP
- Jetson runs lightweight CV model (face/object detection)
- Results displayed on Dashboard

### Phase 5: Sound Source Localization
- ESP32 + INMP441 microphone array
- Direction estimation on ESP32 or Jetson
- Display sound direction on Dashboard

### Phase 6: Integration Demo
- All components working together
- Polished Dashboard for client presentation
- Scenario demos for smart home / industrial use cases

## Key Technical Decisions

| Component | Choice | Reason |
|-----------|--------|--------|
| LLM | Qwen2 7B 4-bit (llama.cpp/Ollama) | Best Chinese support in this size range |
| STT | Whisper small | Balance of accuracy vs memory |
| TTS | VITS / Piper | Lightweight local TTS |
| Communication | MQTT (Mosquitto) | Lightweight, standard IoT protocol |
| Backend | Python + FastAPI + WebSocket | Fast dev, async support |
| Frontend | Web Dashboard | Accessible from any device |
| ESP32 Framework | Arduino or ESP-IDF | Depends on complexity needed |

## Memory Strategy (8GB Jetson)

Models are NOT loaded simultaneously. Sequential pipeline:

| Stage | Model Loaded | Memory Usage |
|-------|-------------|--------------|
| Recording | None | ~0 |
| Speech-to-text | Whisper small | ~1.5GB |
| LLM inference | Qwen2 7B 4-bit | ~4.5GB |
| Text-to-speech | VITS/Piper | ~0.5GB |

Peak: ~4.5GB, leaving headroom for system + other services.

## Future: Phase B (Mobile Robot)

After Phase C is stable:
- Add wheels/chassis
- Motor control via ESP32
- Navigation + obstacle avoidance
- Autonomous movement with voice interaction
