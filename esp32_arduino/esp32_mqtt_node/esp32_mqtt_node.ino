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
 *   - INMP441 I2S microphone recording via /record endpoint
 *   - Audio sent to Jetson/PC for playback (no onboard speaker needed)
 *
 * Wiring:
 *   Pan  Servo (left-right): Signal → GPIO 14, VCC → 5V, GND → GND
 *   Tilt Servo (up-down):    Signal → GPIO 3,  VCC → 5V, GND → GND
 *   DHT11:   Data → GPIO 2, VCC → 3.3V, GND → GND
 *   INMP441: SCK → GPIO 38, WS → GPIO 39, SD → GPIO 40, L/R → GND, VDD → 3.3V
 *
 * Libraries (Arduino IDE → Manage Libraries):
 *   - PubSubClient (Nick O'Leary)
 *   - DHT sensor library (Adafruit)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <driver/i2s.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ===== DHT11 Config =====
#define DHT_PIN   2
#define DHT_TYPE  DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ===== INMP441 I2S Config =====
#define I2S_SCK    38
#define I2S_WS     39
#define I2S_SD     40
#define I2S_PORT   I2S_NUM_0
#define I2S_SAMPLE_RATE  16000
#define I2S_BUF_LEN      512
#define RECORD_SECONDS   3

// ===== Face Detection Types =====
typedef struct {
    int x, y, w, h;
} face_box_t;

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
#define SERVO_TILT_PIN   47   // Up-Down

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
    // Parse query: /servo?pan=90&tilt=90&track=1
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

// ===== Sensor Data API =====
static esp_err_t sensor_handler(httpd_req_t *req) {
    float humidity = dht.readHumidity();
    float temp     = dht.readTemperature();
    char resp[128];
    if (isnan(humidity) || isnan(temp)) {
        snprintf(resp, sizeof(resp), "{\"error\":\"DHT11 read failed\"}");
    } else {
        snprintf(resp, sizeof(resp),
            "{\"temperature\":%.1f,\"humidity\":%.1f}",
            temp, humidity);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// ===== Record Audio Handler =====
static esp_err_t record_handler(httpd_req_t *req) {
    // Parse duration from query: /record?seconds=3
    int seconds = RECORD_SECONDS;
    char query[32] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "seconds", val, sizeof(val)) == ESP_OK) {
            seconds = constrain(atoi(val), 1, 10);
        }
    }

    // Init I2S for recording
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = I2S_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD,
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK ||
        i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "I2S init failed");
        return ESP_FAIL;
    }

    Serial.printf("Recording %d seconds...\n", seconds);

    int totalSamples = I2S_SAMPLE_RATE * seconds;
    int dataSize = totalSamples * 2;  // 16-bit output = 2 bytes per sample
    int wavSize = 44 + dataSize;

    // WAV header
    uint8_t wavHeader[44];
    memcpy(wavHeader, "RIFF", 4);
    *((uint32_t*)(wavHeader + 4)) = wavSize - 8;
    memcpy(wavHeader + 8, "WAVE", 4);
    memcpy(wavHeader + 12, "fmt ", 4);
    *((uint32_t*)(wavHeader + 16)) = 16;
    *((uint16_t*)(wavHeader + 20)) = 1;  // PCM
    *((uint16_t*)(wavHeader + 22)) = 1;  // mono
    *((uint32_t*)(wavHeader + 24)) = I2S_SAMPLE_RATE;
    *((uint32_t*)(wavHeader + 28)) = I2S_SAMPLE_RATE * 2;  // byte rate
    *((uint16_t*)(wavHeader + 32)) = 2;  // block align
    *((uint16_t*)(wavHeader + 34)) = 16; // bits per sample
    memcpy(wavHeader + 36, "data", 4);
    *((uint32_t*)(wavHeader + 40)) = dataSize;

    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Send WAV header
    httpd_resp_send_chunk(req, (const char*)wavHeader, 44);

    // Record and stream in chunks
    int32_t i2sBuf[I2S_BUF_LEN];
    int16_t pcmBuf[I2S_BUF_LEN];
    size_t bytesRead;
    int samplesLeft = totalSamples;

    while (samplesLeft > 0) {
        int toRead = min((int)I2S_BUF_LEN, samplesLeft);
        i2s_read(I2S_PORT, i2sBuf, toRead * sizeof(int32_t), &bytesRead, portMAX_DELAY);
        int got = bytesRead / sizeof(int32_t);

        // Convert 32-bit I2S to 16-bit PCM
        for (int i = 0; i < got; i++) {
            pcmBuf[i] = (int16_t)(i2sBuf[i] >> 14);
        }

        httpd_resp_send_chunk(req, (const char*)pcmBuf, got * sizeof(int16_t));
        samplesLeft -= got;
    }

    // End chunked response
    httpd_resp_send_chunk(req, NULL, 0);

    i2s_driver_uninstall(I2S_PORT);
    Serial.println("Recording done, WAV sent");
    return ESP_OK;
}

// ===== Web Dashboard Page =====
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Robot - ESP32 Control</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#181825;color:#cdd6f4;font-family:system-ui,sans-serif;padding:16px}
h1{font-size:1.3em;margin-bottom:12px;color:#89b4fa}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:900px;margin:0 auto}
.card{background:#1e1e2e;border-radius:10px;padding:14px;border:1px solid #313244}
.card h3{font-size:0.95em;color:#a6adc8;margin-bottom:10px}
.cam-box{grid-column:1/3;text-align:center}
.cam-box img{width:100%;max-width:640px;border-radius:8px;background:#11111b}
.sensor-val{font-size:2em;font-weight:bold;color:#a6e3a1}
.sensor-unit{font-size:0.5em;color:#9399b2}
.sensor-row{display:flex;gap:20px;justify-content:center;margin-top:4px}
.slider-group{margin:8px 0}
.slider-group label{display:flex;justify-content:space-between;font-size:0.85em;color:#9399b2;margin-bottom:4px}
input[type=range]{width:100%;accent-color:#89b4fa;background:#313244;height:6px;border-radius:3px;-webkit-appearance:none;appearance:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:#89b4fa;cursor:pointer}
.btn{padding:8px 16px;border:none;border-radius:6px;cursor:pointer;font-size:0.9em;margin:4px}
.btn-on{background:#a6e3a1;color:#1e1e2e}
.btn-off{background:#f38ba8;color:#1e1e2e}
.btn-center{background:#89b4fa;color:#1e1e2e}
.btn-group{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}
.status{font-size:0.8em;color:#6c7086;margin-top:8px}
.track-badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:0.75em;font-weight:bold}
.track-on{background:#a6e3a1;color:#1e1e2e;animation:pulse 2s infinite}
.track-off{background:#45475a;color:#9399b2}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.6}}
@media(max-width:600px){.grid{grid-template-columns:1fr}.cam-box{grid-column:1}}
</style>
</head>
<body>
<h1>AI Companion Robot - ESP32 Control</h1>
<div class="grid">

<div class="card cam-box">
<h3>Camera <span id="trackBadge" class="track-badge track-on">TRACKING</span></h3>
<img id="stream" src="" alt="Loading camera...">
<div class="btn-group" style="justify-content:center;margin-top:8px">
<button class="btn btn-center" onclick="setServo(90,90)">Center</button>
<button class="btn btn-on" id="btnTrack" onclick="toggleTrack()">Tracking ON</button>
</div>
</div>

<div class="card">
<h3>Temperature</h3>
<div class="sensor-row">
<div class="sensor-val" id="temp">--<span class="sensor-unit"> C</span></div>
</div>
</div>

<div class="card">
<h3>Humidity</h3>
<div class="sensor-row">
<div class="sensor-val" id="hum">--<span class="sensor-unit"> %</span></div>
</div>
</div>

<div class="card" style="grid-column:1/3">
<h3>Servo Control</h3>
<div class="slider-group">
<label><span>Pan (Left-Right)</span><span id="panVal">90</span></label>
<input type="range" id="panSlider" min="20" max="160" value="90" oninput="onSlider()">
</div>
<div class="slider-group">
<label><span>Tilt (Up-Down)</span><span id="tiltVal">90</span></label>
<input type="range" id="tiltSlider" min="60" max="120" value="90" oninput="onSlider()">
</div>
<div class="status" id="servoStatus">Pan: 90 | Tilt: 90</div>
</div>

<div class="card" style="grid-column:1/3">
<h3>Microphone (INMP441)</h3>
<div class="btn-group">
<button class="btn btn-on" id="btnRec" onclick="startRecord()">Record 3s</button>
<button class="btn btn-center" onclick="startRecord(5)">Record 5s</button>
</div>
<div id="recStatus" class="status"></div>
<audio id="audioPlayer" controls style="width:100%;margin-top:8px;display:none"></audio>
</div>

</div>

<script>
var trackOn = true;
var host = location.hostname;
document.getElementById('stream').src = 'http://'+host+':81/stream';

function fetchSensor(){
  fetch('/sensor').then(r=>r.json()).then(d=>{
    if(!d.error){
      document.getElementById('temp').innerHTML=d.temperature.toFixed(1)+'<span class="sensor-unit"> C</span>';
      document.getElementById('hum').innerHTML=d.humidity.toFixed(1)+'<span class="sensor-unit"> %</span>';
    }
  }).catch(()=>{});
}

function setServo(p,t){
  var url='/servo?pan='+p+'&tilt='+t;
  fetch(url).then(r=>r.json()).then(d=>{
    document.getElementById('panSlider').value=d.pan;
    document.getElementById('tiltSlider').value=d.tilt;
    document.getElementById('panVal').textContent=Math.round(d.pan);
    document.getElementById('tiltVal').textContent=Math.round(d.tilt);
    document.getElementById('servoStatus').textContent='Pan: '+Math.round(d.pan)+' | Tilt: '+Math.round(d.tilt);
  }).catch(()=>{});
}

var sliderTimer=null;
function onSlider(){
  var p=document.getElementById('panSlider').value;
  var t=document.getElementById('tiltSlider').value;
  document.getElementById('panVal').textContent=p;
  document.getElementById('tiltVal').textContent=t;
  clearTimeout(sliderTimer);
  sliderTimer=setTimeout(function(){
    fetch('/servo?pan='+p+'&tilt='+t+'&track=0').then(r=>r.json()).then(d=>{
      trackOn=false;
      updateTrackUI();
      document.getElementById('servoStatus').textContent='Pan: '+Math.round(d.pan)+' | Tilt: '+Math.round(d.tilt);
    });
  },100);
}

function toggleTrack(){
  trackOn=!trackOn;
  fetch('/servo?track='+(trackOn?1:0)).then(r=>r.json()).then(d=>{
    trackOn=d.tracking;
    updateTrackUI();
  });
}

function updateTrackUI(){
  var btn=document.getElementById('btnTrack');
  var badge=document.getElementById('trackBadge');
  if(trackOn){
    btn.textContent='Tracking ON';btn.className='btn btn-on';
    badge.textContent='TRACKING';badge.className='track-badge track-on';
  }else{
    btn.textContent='Tracking OFF';btn.className='btn btn-off';
    badge.textContent='MANUAL';badge.className='track-badge track-off';
  }
}

function startRecord(sec){
  sec=sec||3;
  var btn=document.getElementById('btnRec');
  var status=document.getElementById('recStatus');
  var player=document.getElementById('audioPlayer');
  btn.disabled=true;
  status.textContent='Recording '+sec+'s...';
  player.style.display='none';
  fetch('/record?seconds='+sec).then(r=>r.blob()).then(blob=>{
    var url=URL.createObjectURL(blob);
    player.src=url;
    player.style.display='block';
    player.play();
    status.textContent='Done! Playing back...';
    btn.disabled=false;
  }).catch(e=>{
    status.textContent='Error: '+e;
    btn.disabled=false;
  });
}

setInterval(fetchSensor, 2000);
fetchSensor();

setInterval(function(){
  if(trackOn){
    fetch('/servo').then(r=>r.json()).then(d=>{
      document.getElementById('panSlider').value=d.pan;
      document.getElementById('tiltSlider').value=d.tilt;
      document.getElementById('panVal').textContent=Math.round(d.pan);
      document.getElementById('tiltVal').textContent=Math.round(d.tilt);
      document.getElementById('servoStatus').textContent='Pan: '+Math.round(d.pan)+' | Tilt: '+Math.round(d.tilt);
    });
  }
},1000);
</script>
</body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, INDEX_HTML);
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

    httpd_uri_t sensor_uri = {
        .uri       = "/sensor",
        .method    = HTTP_GET,
        .handler   = sensor_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t record_uri = {
        .uri       = "/record",
        .method    = HTTP_GET,
        .handler   = record_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &servo_uri);
        httpd_register_uri_handler(camera_httpd, &sensor_uri);
        httpd_register_uri_handler(camera_httpd, &record_uri);
        Serial.println("Web dashboard + API started on port 80");
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

    float humidity = dht.readHumidity();
    float temp     = dht.readTemperature();

    if (isnan(humidity) || isnan(temp)) {
        Serial.println("DHT11 read failed");
        return;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.1f,\"humidity\":%.1f}",
        temp, humidity);
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

    // DHT11
    dht.begin();
    Serial.println("DHT11 initialized");

    // Servos
    servoSetup();

    // WiFi
    wifi_init();

    // Start camera HTTP servers
    if (WiFi.status() == WL_CONNECTED) {
        startCameraServer();
        Serial.printf("Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
        Serial.printf("Stream:    http://%s:81/stream\n", WiFi.localIP().toString().c_str());
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
