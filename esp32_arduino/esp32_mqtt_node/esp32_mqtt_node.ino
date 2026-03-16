/*
 * AI Companion Robot - ESP32-S3-CAM MQTT Node + Camera Stream
 * Board: GOOUUU ESP32-S3-CAM-N16R8 (OV2640)
 *
 * Arduino IDE Settings:
 *   Board: "ESP32S3 Dev Module"
 *   USB CDC On Boot: "Enabled"
 *   Flash Size: "16MB"
 *   PSRAM: "OPI PSRAM"
 *   Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   Port: COM3
 *
 * Features:
 *   - MJPEG stream on http://ESP32_IP:81/stream
 *   - Snapshot on http://ESP32_IP/capture
 *   - MQTT heartbeat + sensor data
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ===== WiFi Config =====
const char* WIFI_SSID     = "OPPO Reno15 2560";
const char* WIFI_PASSWORD = "00000011";

// ===== MQTT Config =====
const char* MQTT_HOST   = "10.175.143.199";  // Jetson IP
const int   MQTT_PORT   = 1883;
const char* DEVICE_ID   = "esp32s3_cam_001";

// ===== Camera Pins (GOOUUU ESP32-S3-CAM / ESP32-S3-EYE) =====
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM     8
#define Y3_GPIO_NUM     9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM  13

// ===== MQTT Topics =====
String topicHeartbeat;
String topicSensor;
String topicCommand;
String topicStatus;

// ===== Intervals =====
const unsigned long HEARTBEAT_INTERVAL = 5000;
const unsigned long SENSOR_INTERVAL    = 2000;

// ===== Objects =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

unsigned long lastHeartbeat = 0;
unsigned long lastSensor    = 0;
unsigned long lastReconnect = 0;

// ===== Camera Init =====
bool camera_init() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }
    Serial.println("Camera initialized!");
    return true;
}

// ===== MJPEG Stream Handler =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

        esp_camera_fb_return(fb);

        if (res != ESP_OK) break;
    }
    return res;
}

// ===== Snapshot Handler =====
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// ===== Start HTTP Servers =====
void startCameraServer() {
    // Stream server on port 81
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("Stream server started on port 81");
    }

    // Capture server on port 80
    config.server_port = 80;
    config.ctrl_port = 32768;

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        Serial.println("Capture server started on port 80");
    }
}

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
    if (mqttClient.connect(DEVICE_ID, NULL, NULL,
                           topicStatus.c_str(), 1, true,
                           "{\"status\":\"offline\"}")) {
        Serial.println(" connected!");
        mqttClient.publish(topicStatus.c_str(), "{\"status\":\"online\"}", true);
        mqttClient.subscribe(topicCommand.c_str());

        // Publish camera stream URL
        char streamUrl[128];
        snprintf(streamUrl, sizeof(streamUrl),
            "{\"stream_url\":\"http://%s:81/stream\",\"capture_url\":\"http://%s/capture\"}",
            WiFi.localIP().toString().c_str(),
            WiFi.localIP().toString().c_str());
        String topicCamera = String("robot/esp32/") + DEVICE_ID + "/camera";
        mqttClient.publish(topicCamera.c_str(), streamUrl, true);
    } else {
        Serial.printf(" failed, rc=%d\n", mqttClient.state());
    }
}

// ===== Publish Heartbeat =====
void mqtt_publish_heartbeat() {
    if (!mqttClient.connected()) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"device\":\"%s\",\"uptime\":%lu,\"heap\":%lu,\"rssi\":%d,\"stream\":\"http://%s:81/stream\"}",
        DEVICE_ID,
        millis() / 1000,
        (unsigned long)ESP.getFreeHeap(),
        WiFi.RSSI(),
        WiFi.localIP().toString().c_str());
    mqttClient.publish(topicHeartbeat.c_str(), payload);
}

// ===== Publish Sensor Data =====
void mqtt_publish_sensor() {
    if (!mqttClient.connected()) return;
    char payload[256];
    float temp     = 22.0 + (random(0, 100) / 20.0);
    float humidity = 40.0 + (random(0, 100) / 5.0);
    int   light    = random(200, 800);
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

    // Camera
    if (!camera_init()) {
        Serial.println("WARNING: Camera failed, continuing without camera");
    }

    // WiFi
    wifi_init();

    // Start camera HTTP servers
    if (WiFi.status() == WL_CONNECTED) {
        startCameraServer();
        Serial.printf("Stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
        Serial.printf("Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
    }

    // MQTT
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    mqtt_connect();
}

// ===== Loop =====
void loop() {
    mqttClient.loop();

    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting...");
        wifi_init();
    }

    if (!mqttClient.connected() && now - lastReconnect > 5000) {
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
        mqtt_publish_sensor();
        Serial.println("Sensor data sent");
    }
}
