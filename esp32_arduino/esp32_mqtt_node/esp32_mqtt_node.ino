/*
 * AI Companion Robot - ESP32-S3-CAM MQTT Node + Camera Stream + Face Tracking
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
 *   - MQTT heartbeat + sensor data + tracking status
 *   - Pan-Tilt face tracking with 2x SG90 servos
 *
 * Wiring:
 *   Pan  Servo (left-right): Signal → GPIO 14, VCC → 5V, GND → GND
 *   Tilt Servo (up-down):    Signal → GPIO 3,  VCC → 5V, GND → GND
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

// ===== Servo Config =====
#define SERVO_PAN_PIN   14   // Left-Right
#define SERVO_TILT_PIN   3   // Up-Down

// LEDC channels (camera uses channel 0)
#define SERVO_PAN_CHANNEL   2
#define SERVO_TILT_CHANNEL  3
#define SERVO_FREQ         50    // 50Hz for SG90
#define SERVO_RESOLUTION   16    // 16-bit resolution

// SG90 pulse width in microseconds
#define SERVO_MIN_US      500
#define SERVO_MAX_US     2400

// Servo angle limits (degrees)
#define PAN_MIN   20
#define PAN_MAX  160
#define TILT_MIN  60
#define TILT_MAX 120

// ===== Tracking Config =====
#define TRACK_INTERVAL_MS    150   // How often to run face detection (ms)
#define TRACK_DEADZONE        20   // Pixels from center to ignore (prevent jitter)
#define TRACK_GAIN_PAN      0.15f  // P-control gain for pan
#define TRACK_GAIN_TILT     0.12f  // P-control gain for tilt
#define TRACK_SMOOTHING     0.6f   // Smoothing factor (0-1, higher = smoother)
#define LOST_TIMEOUT_MS     3000   // Return to center after losing face (ms)

// Detection frame size (smaller = faster detection)
#define DETECT_WIDTH   320
#define DETECT_HEIGHT  240

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
unsigned long lastTrack     = 0;
unsigned long lastFaceSeen  = 0;

// Servo state
float panAngle  = 90.0;  // Current pan angle
float tiltAngle = 90.0;  // Current tilt angle
float targetPan  = 90.0;
float targetTilt = 90.0;
bool  tracking   = false;
bool  trackingEnabled = true;  // Can be toggled via MQTT

// ===== Servo Functions =====
void servoSetup() {
    ledcAttach(SERVO_PAN_PIN, SERVO_FREQ, SERVO_RESOLUTION);
    ledcAttach(SERVO_TILT_PIN, SERVO_FREQ, SERVO_RESOLUTION);
    servoWrite(SERVO_PAN_PIN, 90);
    servoWrite(SERVO_TILT_PIN, 90);
    Serial.println("Servos initialized (center position)");
}

void servoWrite(int pin, float angle) {
    angle = constrain(angle, 0, 180);
    // Convert angle to pulse width in microseconds
    uint32_t pulseUs = map(angle * 10, 0, 1800, SERVO_MIN_US, SERVO_MAX_US);
    // Convert microseconds to duty cycle (16-bit resolution, 50Hz = 20000us period)
    uint32_t duty = (pulseUs * ((1 << SERVO_RESOLUTION) - 1)) / 20000;
    ledcWrite(pin, duty);
}

// ===== Face Detection =====
// Simple face detection using skin color in YUV/RGB
// Works without external libraries, reliable for demo
typedef struct {
    int x, y, w, h;
} face_box_t;

bool detectFaceSimple(camera_fb_t* fb, face_box_t* result) {
    // We need RGB565 or RGB888 for color-based detection
    // Convert JPEG to RGB888
    if (fb->format != PIXFORMAT_JPEG) return false;

    // Use a simplified approach: decode JPEG, scan for skin-colored regions
    // For efficiency, we'll work with the raw JPEG and use a simpler method

    // Alternative: Use frame differencing or brightness-based detection
    // For demo, we'll use a grid-based skin color detector

    // Allocate RGB buffer
    uint8_t* rgb_buf = (uint8_t*)ps_malloc(fb->width * fb->height * 3);
    if (!rgb_buf) return false;

    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf);
    if (!converted) {
        free(rgb_buf);
        return false;
    }

    int w = fb->width;
    int h = fb->height;

    // Scan with grid (every 4th pixel for speed)
    int step = 4;
    int skinCount = 0;
    long sumX = 0, sumY = 0;
    int minX = w, minY = h, maxX = 0, maxY = 0;

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            int idx = (y * w + x) * 3;
            uint8_t r = rgb_buf[idx];
            uint8_t g = rgb_buf[idx + 1];
            uint8_t b = rgb_buf[idx + 2];

            // Skin color detection in RGB
            // Relaxed thresholds for various skin tones
            if (r > 80 && g > 40 && b > 20 &&
                r > g && r > b &&
                (r - g) > 15 &&
                (int)r - (int)b > 15 &&
                abs((int)g - (int)b) < 80) {

                skinCount++;
                sumX += x;
                sumY += y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    free(rgb_buf);

    // Need minimum skin pixels to count as a face
    int minPixels = (w * h) / (step * step * 50);  // At least 2% of grid
    int maxPixels = (w * h) / (step * step * 3);    // No more than 33%

    if (skinCount < minPixels || skinCount > maxPixels) return false;

    // Check aspect ratio (face is roughly square to slightly tall)
    int bw = maxX - minX;
    int bh = maxY - minY;
    if (bw < 20 || bh < 20) return false;
    float aspect = (float)bw / (float)bh;
    if (aspect < 0.4 || aspect > 2.0) return false;

    result->x = minX;
    result->y = minY;
    result->w = bw;
    result->h = bh;

    return true;
}

// ===== Tracking Logic =====
void updateTracking() {
    if (!trackingEnabled) return;

    unsigned long now = millis();
    if (now - lastTrack < TRACK_INTERVAL_MS) return;
    lastTrack = now;

    // Temporarily switch to QVGA for faster detection
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    framesize_t origSize = s->status.framesize;
    s->set_framesize(s, FRAMESIZE_QVGA);  // 320x240
    delay(10);

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        s->set_framesize(s, origSize);
        return;
    }

    face_box_t face;
    bool found = detectFaceSimple(fb, &face);

    if (found) {
        lastFaceSeen = now;
        tracking = true;

        // Calculate face center
        float faceCX = face.x + face.w / 2.0;
        float faceCY = face.y + face.h / 2.0;

        // Calculate offset from image center (normalized -1 to 1)
        float errX = (faceCX - DETECT_WIDTH / 2.0) / (DETECT_WIDTH / 2.0);
        float errY = (faceCY - DETECT_HEIGHT / 2.0) / (DETECT_HEIGHT / 2.0);

        // Apply deadzone
        if (abs(errX) * DETECT_WIDTH / 2.0 < TRACK_DEADZONE) errX = 0;
        if (abs(errY) * DETECT_HEIGHT / 2.0 < TRACK_DEADZONE) errY = 0;

        // P-control: calculate target angles
        // Pan: face moves right in image → servo moves left (decrease angle)
        targetPan  = panAngle - errX * TRACK_GAIN_PAN * 90.0;
        // Tilt: face moves down in image → servo tilts down
        targetTilt = tiltAngle + errY * TRACK_GAIN_TILT * 90.0;

        // Clamp to limits
        targetPan  = constrain(targetPan, PAN_MIN, PAN_MAX);
        targetTilt = constrain(targetTilt, TILT_MIN, TILT_MAX);
    } else {
        // No face detected
        if (tracking && (now - lastFaceSeen > LOST_TIMEOUT_MS)) {
            // Return to center slowly
            targetPan  = 90.0;
            targetTilt = 90.0;
            tracking = false;
        }
    }

    esp_camera_fb_return(fb);

    // Restore original frame size for streaming
    s->set_framesize(s, origSize);

    // Smooth servo movement
    panAngle  = panAngle  + TRACK_SMOOTHING * (targetPan  - panAngle);
    tiltAngle = tiltAngle + TRACK_SMOOTHING * (targetTilt - tiltAngle);

    // Write to servos
    servoWrite(SERVO_PAN_PIN, panAngle);
    servoWrite(SERVO_TILT_PIN, tiltAngle);
}

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
    config.frame_size   = FRAMESIZE_VGA;  // 640x480 for streaming
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

// ===== Servo Control HTTP Handler =====
static esp_err_t servo_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse simple query: /servo?pan=90&tilt=90
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "pan", val, sizeof(val)) == ESP_OK) {
            float p = atof(val);
            panAngle = constrain(p, PAN_MIN, PAN_MAX);
            targetPan = panAngle;
            servoWrite(SERVO_PAN_PIN, panAngle);
        }
        if (httpd_query_key_value(query, "tilt", val, sizeof(val)) == ESP_OK) {
            float t = atof(val);
            tiltAngle = constrain(t, TILT_MIN, TILT_MAX);
            targetTilt = tiltAngle;
            servoWrite(SERVO_TILT_PIN, tiltAngle);
        }
        if (httpd_query_key_value(query, "track", val, sizeof(val)) == ESP_OK) {
            trackingEnabled = (atoi(val) == 1);
        }
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"pan\":%.1f,\"tilt\":%.1f,\"tracking\":%s}",
        panAngle, tiltAngle, trackingEnabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
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

    // Capture + Servo server on port 80
    config.server_port = 80;
    config.ctrl_port = 32768;

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t servo_uri = {
        .uri       = "/servo",
        .method    = HTTP_GET,
        .handler   = servo_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &servo_uri);
        Serial.println("Capture + Servo server started on port 80");
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

    // Handle servo commands via MQTT
    // Example: {"pan":90,"tilt":90} or {"tracking":true}
    if (msg.indexOf("\"pan\"") >= 0) {
        int idx = msg.indexOf("\"pan\":") + 6;
        float p = msg.substring(idx).toFloat();
        if (p > 0) {
            panAngle = constrain(p, PAN_MIN, PAN_MAX);
            targetPan = panAngle;
            servoWrite(SERVO_PAN_PIN, panAngle);
        }
    }
    if (msg.indexOf("\"tilt\"") >= 0) {
        int idx = msg.indexOf("\"tilt\":") + 7;
        float t = msg.substring(idx).toFloat();
        if (t > 0) {
            tiltAngle = constrain(t, TILT_MIN, TILT_MAX);
            targetTilt = tiltAngle;
            servoWrite(SERVO_TILT_PIN, tiltAngle);
        }
    }
    if (msg.indexOf("\"tracking\"") >= 0) {
        trackingEnabled = (msg.indexOf("true") >= 0);
        Serial.printf("Tracking %s\n", trackingEnabled ? "enabled" : "disabled");
    }
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
    char payload[384];
    snprintf(payload, sizeof(payload),
        "{\"device\":\"%s\",\"uptime\":%lu,\"heap\":%lu,\"rssi\":%d,"
        "\"stream\":\"http://%s:81/stream\","
        "\"tracking\":%s,\"pan\":%.1f,\"tilt\":%.1f}",
        DEVICE_ID,
        millis() / 1000,
        (unsigned long)ESP.getFreeHeap(),
        WiFi.RSSI(),
        WiFi.localIP().toString().c_str(),
        tracking ? "true" : "false",
        panAngle, tiltAngle);
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
    Serial.println("\n=== AI Companion Robot - ESP32-S3-CAM + Face Tracking ===");

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

    // Servos
    servoSetup();

    // WiFi
    wifi_init();

    // Start camera HTTP servers
    if (WiFi.status() == WL_CONNECTED) {
        startCameraServer();
        Serial.printf("Stream:  http://%s:81/stream\n", WiFi.localIP().toString().c_str());
        Serial.printf("Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
        Serial.printf("Servo:   http://%s/servo?pan=90&tilt=90&track=1\n", WiFi.localIP().toString().c_str());
    }

    // MQTT
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    mqtt_connect();

    Serial.println("=== Ready! Face tracking enabled ===");
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
    }

    if (now - lastSensor > SENSOR_INTERVAL) {
        lastSensor = now;
        mqtt_publish_sensor();
    }

    // Face tracking (runs every TRACK_INTERVAL_MS)
    updateTracking();
}
