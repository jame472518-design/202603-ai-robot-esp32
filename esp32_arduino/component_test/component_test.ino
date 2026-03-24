/*
 * Component Test - ESP32-S3-CAM
 * 逐一测试每个元件，Serial Monitor 查看结果
 *
 * Arduino IDE Settings:
 *   Board: "ESP32S3 Dev Module"
 *   USB CDC On Boot: "Enabled"
 *   PSRAM: "OPI PSRAM"
 *
 * Wiring:
 *   SG90 Pan:  Signal → GPIO 14, VCC → 5V, GND → GND
 *   SG90 Tilt: Signal → GPIO 3,  VCC → 5V, GND → GND
 *   DHT11:     Data   → GPIO 2,  VCC → 3.3V, GND → GND
 *
 * Usage:
 *   Open Serial Monitor (115200 baud)
 *   Type command and press Enter:
 *     1 = Test DHT11
 *     2 = Test Servo Pan (GPIO 14)
 *     3 = Test Servo Tilt (GPIO 3)
 *     4 = Test Camera
 *     5 = Test WiFi
 *     6 = Servo sweep (continuous)
 *     7 = Test ALL
 *     s = Stop servo sweep
 */

#include <DHT.h>
#include "esp_camera.h"

// ===== Pin Config =====
#define SERVO_PAN_PIN   14
#define SERVO_TILT_PIN   3
#define DHT_PIN           2
#define DHT_TYPE       DHT11

// ===== Camera Pins (GOOUUU ESP32-S3-CAM) =====
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

// ===== WiFi =====
const char* WIFI_SSID     = "OPPO Reno15 2560";
const char* WIFI_PASSWORD = "00000011";

// ===== Objects =====
DHT dht(DHT_PIN, DHT_TYPE);
bool cameraReady = false;
bool sweeping = false;

// ===== Servo =====
#define SERVO_FREQ       50
#define SERVO_RESOLUTION 14    // 14-bit
#define SERVO_MIN_US    500
#define SERVO_MAX_US   2400

void servoWrite(int pin, float angle) {
    angle = constrain(angle, 0, 180);
    uint32_t pulseUs = map(angle * 10, 0, 1800, SERVO_MIN_US, SERVO_MAX_US);
    // 14-bit, 50Hz = 20000us period
    uint32_t maxDuty = (1 << SERVO_RESOLUTION) - 1;
    uint32_t duty = (pulseUs * maxDuty) / 20000;
    ledcWrite(pin, duty);
    Serial.printf("  Pin %d → angle=%.0f, pulse=%uus, duty=%u/%u\n",
                  pin, angle, pulseUs, duty, maxDuty);
}

// ===== Test Functions =====

void testDHT11() {
    Serial.println("\n===== TEST: DHT11 (GPIO 2) =====");
    Serial.println("Reading 3 times...\n");

    for (int i = 0; i < 3; i++) {
        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (isnan(h) || isnan(t)) {
            Serial.printf("  [%d] FAIL - Read error (NaN)\n", i + 1);
            Serial.println("  Check: wiring, VCC=3.3V, Data=GPIO2");
        } else {
            Serial.printf("  [%d] OK - Temp: %.1f°C, Humidity: %.1f%%\n", i + 1, t, h);
        }
        delay(2200);  // DHT11 needs 2s between reads
    }
    Serial.println("===== DHT11 TEST DONE =====\n");
}

void testServoPan() {
    Serial.println("\n===== TEST: Servo Pan (GPIO 14) =====");
    Serial.println("Moving: center → left → right → center\n");

    Serial.println("  → Center (90°)");
    servoWrite(SERVO_PAN_PIN, 90);
    delay(1000);

    Serial.println("  → Left (30°)");
    servoWrite(SERVO_PAN_PIN, 30);
    delay(1000);

    Serial.println("  → Right (150°)");
    servoWrite(SERVO_PAN_PIN, 150);
    delay(1000);

    Serial.println("  → Center (90°)");
    servoWrite(SERVO_PAN_PIN, 90);
    delay(500);

    Serial.println("\nDid the servo move? If not:");
    Serial.println("  - Check VCC is connected to 5V (not 3.3V)");
    Serial.println("  - Check signal wire is on GPIO 14");
    Serial.println("  - Try a different servo");
    Serial.println("===== PAN TEST DONE =====\n");
}

void testServoTilt() {
    Serial.println("\n===== TEST: Servo Tilt (GPIO 3) =====");
    Serial.println("Moving: center → up → down → center\n");

    Serial.println("  → Center (90°)");
    servoWrite(SERVO_TILT_PIN, 90);
    delay(1000);

    Serial.println("  → Up (60°)");
    servoWrite(SERVO_TILT_PIN, 60);
    delay(1000);

    Serial.println("  → Down (120°)");
    servoWrite(SERVO_TILT_PIN, 120);
    delay(1000);

    Serial.println("  → Center (90°)");
    servoWrite(SERVO_TILT_PIN, 90);
    delay(500);

    Serial.println("\nDid the servo move? If not:");
    Serial.println("  - Check VCC is connected to 5V (not 3.3V)");
    Serial.println("  - Check signal wire is on GPIO 3");
    Serial.println("  - GPIO 3 might be reserved on your board, try GPIO 47");
    Serial.println("===== TILT TEST DONE =====\n");
}

void testCamera() {
    Serial.println("\n===== TEST: Camera (OV2640) =====");

    if (!cameraReady) {
        Serial.println("  Camera not initialized, trying now...");
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
        config.frame_size   = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;

        esp_err_t err = esp_camera_init(&config);
        if (err != ESP_OK) {
            Serial.printf("  FAIL - Camera init error: 0x%x\n", err);
            Serial.println("===== CAMERA TEST DONE =====\n");
            return;
        }
        cameraReady = true;
        Serial.println("  Camera initialized OK");
    }

    // Try to capture a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("  FAIL - Could not capture frame");
    } else {
        Serial.printf("  OK - Captured frame: %dx%d, %u bytes, format=%d\n",
                      fb->width, fb->height, fb->len, fb->format);
        esp_camera_fb_return(fb);
    }

    Serial.printf("  Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("  PSRAM: %u / %u bytes free\n", ESP.getFreePsram(), ESP.getPsramSize());
    Serial.println("===== CAMERA TEST DONE =====\n");
}

void testWiFi() {
    Serial.println("\n===== TEST: WiFi =====");
    Serial.printf("  SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("  Connecting");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n  OK - Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\n  FAIL - Could not connect");
        Serial.println("  Check: SSID, password, 2.4GHz only");
    }
    WiFi.disconnect();
    Serial.println("===== WIFI TEST DONE =====\n");
}

void servoSweep() {
    Serial.println("\n===== Servo Sweep (type 's' to stop) =====\n");
    sweeping = true;

    while (sweeping) {
        for (int angle = 30; angle <= 150 && sweeping; angle += 5) {
            servoWrite(SERVO_PAN_PIN, angle);
            delay(100);
            if (Serial.available()) {
                char c = Serial.read();
                if (c == 's' || c == 'S') sweeping = false;
            }
        }
        for (int angle = 150; angle >= 30 && sweeping; angle -= 5) {
            servoWrite(SERVO_PAN_PIN, angle);
            delay(100);
            if (Serial.available()) {
                char c = Serial.read();
                if (c == 's' || c == 'S') sweeping = false;
            }
        }
    }

    servoWrite(SERVO_PAN_PIN, 90);
    Serial.println("===== Sweep stopped =====\n");
}

void testAll() {
    Serial.println("\n##############################");
    Serial.println("# TESTING ALL COMPONENTS     #");
    Serial.println("##############################\n");

    testDHT11();
    testServoPan();
    testServoTilt();
    testCamera();
    testWiFi();

    Serial.println("##############################");
    Serial.println("# ALL TESTS COMPLETE         #");
    Serial.println("##############################\n");
}

void printMenu() {
    Serial.println("================================");
    Serial.println("  Component Test Menu");
    Serial.println("================================");
    Serial.println("  1 = DHT11 (temp/humidity)");
    Serial.println("  2 = Servo Pan  (GPIO 14)");
    Serial.println("  3 = Servo Tilt (GPIO 3)");
    Serial.println("  4 = Camera");
    Serial.println("  5 = WiFi");
    Serial.println("  6 = Servo sweep (continuous)");
    Serial.println("  7 = Test ALL");
    Serial.println("  s = Stop sweep");
    Serial.println("================================");
    Serial.println("Type a number and press Enter:\n");
}

// ===== Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== ESP32-S3-CAM Component Tester ===\n");

    // Init DHT11
    dht.begin();
    Serial.println("DHT11 ready (GPIO 2)");

    // Init Servos
    ledcAttach(SERVO_PAN_PIN, SERVO_FREQ, SERVO_RESOLUTION);
    ledcAttach(SERVO_TILT_PIN, SERVO_FREQ, SERVO_RESOLUTION);
    servoWrite(SERVO_PAN_PIN, 90);
    servoWrite(SERVO_TILT_PIN, 90);
    Serial.println("Servos ready (GPIO 14, GPIO 3)");

    Serial.println("");
    printMenu();
}

// ===== Loop =====
void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        // Flush remaining chars (newline etc)
        while (Serial.available()) Serial.read();

        switch (cmd) {
            case '1': testDHT11(); printMenu(); break;
            case '2': testServoPan(); printMenu(); break;
            case '3': testServoTilt(); printMenu(); break;
            case '4': testCamera(); printMenu(); break;
            case '5': testWiFi(); printMenu(); break;
            case '6': servoSweep(); printMenu(); break;
            case '7': testAll(); printMenu(); break;
            case 's':
            case 'S': sweeping = false; break;
            default: break;
        }
    }
}
