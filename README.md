# AI Companion Robot Demo

ESP32 edge sensors + Jetson Orin 8GB AI companion robot demo system.

## Architecture

- `esp32/` - ESP32 PlatformIO project (edge sensors, MQTT publisher)
- `jetson/` - Jetson Python backend (FastAPI, AI models, MQTT subscriber)
- `frontend/` - Vue 3 Web Dashboard
- `docs/` - Design documents and plans
