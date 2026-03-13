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
