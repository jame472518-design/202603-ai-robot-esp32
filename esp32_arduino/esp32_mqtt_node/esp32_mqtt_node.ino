/*
 * AI Companion Robot - ESP32-S3-CAM MQTT Node
 * Board: GOOUUU ESP32-S3-CAM-N16R8 (OV2640)
 *
 * Arduino IDE Settings:
 *   Board: "ESP32S3 Dev Module"
 *   USB CDC On Boot: "Enabled"
 *   Flash Size: "16MB"
 *   PSRAM: "OPI PSRAM"
 *   Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   Port: COM3
 */

#include <WiFi.h>
#include <PubSubClient.h>

// ===== WiFi Config =====
const char* WIFI_SSID     = "OPPO Reno15 2560";
const char* WIFI_PASSWORD = "00000011";

// ===== MQTT Config =====
const char* MQTT_HOST   = "10.175.143.199";  // Jetson IP
const int   MQTT_PORT   = 1883;
const char* DEVICE_ID   = "esp32s3_cam_001";

// ===== MQTT Topics =====
// PubSubClient doesn't support compile-time string concat, so we build at runtime
String topicHeartbeat;
String topicSensor;
String topicCommand;
String topicStatus;

// ===== Intervals =====
const unsigned long HEARTBEAT_INTERVAL = 5000;  // 5s
const unsigned long SENSOR_INTERVAL    = 2000;  // 2s

// ===== Objects =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastHeartbeat = 0;
unsigned long lastSensor    = 0;
unsigned long lastReconnect = 0;

// ===== WiFi =====
void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

// ===== MQTT Callback =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    Serial.printf("Received [%s]: %s\n", topic, msg.c_str());
}

// ===== MQTT Connect =====
void mqtt_connect() {
    if (mqttClient.connected()) return;

    Serial.print("Connecting to MQTT...");
    // Set last will (offline status)
    if (mqttClient.connect(DEVICE_ID, NULL, NULL,
                           topicStatus.c_str(), 1, true,
                           "{\"status\":\"offline\"}")) {
        Serial.println(" connected!");
        // Publish online status
        mqttClient.publish(topicStatus.c_str(), "{\"status\":\"online\"}", true);
        // Subscribe to commands
        mqttClient.subscribe(topicCommand.c_str());
    } else {
        Serial.printf(" failed, rc=%d\n", mqttClient.state());
    }
}

// ===== Publish Heartbeat =====
void mqtt_publish_heartbeat() {
    if (!mqttClient.connected()) return;
    char payload[200];
    snprintf(payload, sizeof(payload),
        "{\"device\":\"%s\",\"uptime\":%lu,\"heap\":%lu,\"rssi\":%d}",
        DEVICE_ID,
        millis() / 1000,
        (unsigned long)ESP.getFreeHeap(),
        WiFi.RSSI());
    mqttClient.publish(topicHeartbeat.c_str(), payload);
}

// ===== Publish Sensor Data =====
void mqtt_publish_sensor() {
    if (!mqttClient.connected()) return;
    char payload[256];
    float temp     = 22.0 + (random(0, 100) / 20.0);   // 22-27 C
    float humidity = 40.0 + (random(0, 100) / 5.0);     // 40-60 %
    int   light    = random(200, 800);                    // lux
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%d}",
        temp, humidity, light);
    mqttClient.publish(topicSensor.c_str(), payload);
}

// ===== Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== AI Companion Robot - ESP32-S3-CAM Node ===");

    // Build topic strings
    String prefix = "robot/esp32/";
    prefix += DEVICE_ID;
    topicHeartbeat = prefix + "/heartbeat";
    topicSensor    = prefix + "/sensor";
    topicCommand   = prefix + "/command";
    topicStatus    = prefix + "/status";

    // WiFi
    wifi_init();

    // MQTT
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    mqtt_connect();
}

// ===== Loop =====
void loop() {
    // Keep MQTT alive
    mqttClient.loop();

    unsigned long now = millis();

    // Reconnect WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting...");
        wifi_init();
    }

    // Reconnect MQTT (every 5s)
    if (!mqttClient.connected() && now - lastReconnect > 5000) {
        lastReconnect = now;
        mqtt_connect();
    }

    // Heartbeat
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        lastHeartbeat = now;
        mqtt_publish_heartbeat();
        Serial.println("Heartbeat sent");
    }

    // Sensor data
    if (now - lastSensor > SENSOR_INTERVAL) {
        lastSensor = now;
        mqtt_publish_sensor();
        Serial.println("Sensor data sent");
    }
}
