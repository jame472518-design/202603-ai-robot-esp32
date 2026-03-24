# Arduino IDE 設定 - ESP32-S3-CAM (GOOUUU N16R8)

## Board Settings

| 設定項目 | 值 |
|---------|-----|
| Board | **ESP32S3 Dev Module** |
| USB CDC On Boot | **Enabled** |
| CPU Frequency | 240MHz (default) |
| Flash Mode | QIO 80MHz (default) |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Upload Speed | 921600 (default) |
| Port | 你的 COM port (例如 COM3) |

## 需要安裝的 Libraries

Arduino IDE → Sketch → Include Library → Manage Libraries:

| Library | Author | 用途 |
|---------|--------|------|
| PubSubClient | Nick O'Leary | MQTT 通訊 |
| DHT sensor library | Adafruit | DHT11 溫溼度 |

> 安裝 DHT sensor library 時會提示安裝 **Adafruit Unified Sensor**，選 Yes。

## Serial Monitor

| 設定 | 值 |
|------|-----|
| Baud Rate | **115200** |
| Line Ending | Newline 或 Both NL & CR |

## 接線圖

```
ESP32-S3-CAM
┌─────────────────┐
│                  │
│  5V  ──┬──────── SG90 Pan 紅線 (VCC)
│        └──────── SG90 Tilt 紅線 (VCC)
│                  │
│  GND ──┬──────── SG90 Pan 棕線 (GND)
│        ├──────── SG90 Tilt 棕線 (GND)
│        └──────── DHT11 GND
│                  │
│  3.3V ────────── DHT11 VCC
│                  │
│  GPIO 14 ─────── SG90 Pan 橙線 (信號)
│  GPIO 3  ─────── SG90 Tilt 橙線 (信號)
│  GPIO 2  ─────── DHT11 Data
│                  │
└─────────────────┘

SG90 線色對照:
  橙/黃 = 信號 (Signal)
  紅   = 電源 (VCC 5V)
  棕/黑 = 接地 (GND)

DHT11 (3腳模組):
  VCC  = 3.3V
  DATA = GPIO 2
  GND  = GND
```

## 程式位置

| 程式 | 路徑 | 用途 |
|------|------|------|
| 主程式 | `esp32_arduino/esp32_mqtt_node/esp32_mqtt_node.ino` | 完整功能 (Camera + MQTT + Servo + DHT11) |
| 測試程式 | `esp32_arduino/component_test/component_test.ino` | 逐一測試各元件 |

## 注意事項

- ESP32 只支援 **2.4GHz WiFi**，不支援 5GHz
- SG90 必須接 **5V**，接 3.3V 不會動
- 5V 和 GND 只有各 1 個，需要用面包板或併線分出來
- 上傳前確認 USB CDC On Boot 是 **Enabled**，否則 Serial Monitor 不會有輸出
