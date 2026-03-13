#pragma once
#include <espMqttClient.h>
#include "config.h"

espMqttClient mqttClient;
bool mqttConnected = false;

void onMqttConnect(bool sessionPresent) {
    Serial.println("MQTT connected");
    mqttConnected = true;
    mqttClient.subscribe(TOPIC_COMMAND, 1);
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
}

void mqtt_init() {
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setClientId(DEVICE_ID);
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
