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

    if (!wifi_connected()) {
        wifi_init();
    }

    if (!mqttConnected && now - lastReconnect > 5000) {
        lastReconnect = now;
        mqtt_connect();
    }

    if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        lastHeartbeat = now;
        mqtt_publish_heartbeat();
        Serial.println("Heartbeat sent");
    }

    if (now - lastSensor > SENSOR_INTERVAL) {
        lastSensor = now;
        char sensorJson[256];
        float temp = 22.0 + (random(0, 100) / 20.0);
        float humidity = 40.0 + (random(0, 100) / 5.0);
        int light = random(200, 800);
        snprintf(sensorJson, sizeof(sensorJson),
            "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%d}",
            temp, humidity, light);
        mqtt_publish_sensor(sensorJson);
    }
}
