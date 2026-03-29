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
 *   OLED:      SDA → GPIO 47, SCL → GPIO 48, VCC → 3.3V, GND → GND
 *   INMP441:   SCK → GPIO 38, WS → GPIO 39, SD → GPIO 40, L/R → GND, VDD → 3.3V
 *   MAX98357A: BCLK → GPIO 41, LRC → GPIO 42, DIN → GPIO 1, VIN → 5V
 *
 * Libraries:
 *   - DHT sensor library (Adafruit)
 *   - U8g2
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
 *     8 = Test OLED
 *     9 = OLED live sensor display
 *     m = Test INMP441 microphone
 *     p = Test MAX98357A speaker
 *     l = Mic loopback (mic → speaker live)
 *     s = Stop sweep / live / mic / loopback
 */

#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <U8g2lib.h>
#include <driver/i2s.h>
#include "esp_camera.h"

// ===== Pin Config =====
#define SERVO_PAN_PIN   14
#define SERVO_TILT_PIN   3
#define DHT_PIN           2
#define DHT_TYPE       DHT11

// ===== OLED Config =====
#define OLED_SDA       47
#define OLED_SCL       48
#define OLED_WIDTH    128
#define OLED_HEIGHT    64
#define OLED_ADDR    0x3C

// SH1106 1.3" OLED via I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
bool oledReady = false;

// Servo angle tracking (for OLED display)
float panAngle = 90.0;
float tiltAngle = 90.0;

// ===== INMP441 I2S Config =====
#define I2S_SCK    38
#define I2S_WS     39
#define I2S_SD     40
#define I2S_PORT   I2S_NUM_0
#define I2S_SAMPLE_RATE  16000
#define I2S_BUF_LEN      512
bool micReady = false;

// ===== MAX98357A I2S Config =====
#define SPK_BCLK   41
#define SPK_LRC    42
#define SPK_DIN     1
#define SPK_PORT   I2S_NUM_1
bool spkReady = false;

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

void initOLED() {
    if (oledReady) return;
    Wire.begin(OLED_SDA, OLED_SCL);
    u8g2.setBusClock(400000);
    u8g2.begin();
    oledReady = true;
    Serial.println("  OLED (SH1106) initialized OK");
}

void testOLED() {
    Serial.println("\n===== TEST: OLED 1.3\" SH1106 (GPIO 47/48) =====");

    initOLED();

    // Test 1: Text
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "AI Companion Robot");
    u8g2.drawLine(0, 12, 128, 12);
    u8g2.drawStr(0, 26, "OLED Test OK!");
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(20, 50, "Hello!");
    u8g2.sendBuffer();
    Serial.println("  OK - Text displayed");
    delay(2000);

    // Test 2: Shapes
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawDisc(64, 32, 20);
    u8g2.sendBuffer();
    Serial.println("  OK - Shapes displayed");
    delay(2000);

    // Test 3: Progress bar animation
    for (int i = 0; i <= 100; i += 5) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(30, 20, "Loading...");
        u8g2.drawFrame(14, 30, 100, 14);
        u8g2.drawBox(14, 30, i, 14);
        u8g2.sendBuffer();
        delay(50);
    }
    Serial.println("  OK - Animation displayed");
    delay(1000);

    Serial.println("===== OLED TEST DONE =====\n");
}

void oledLiveSensor() {
    Serial.println("\n===== OLED Live Sensor (type 's' to stop) =====");
    initOLED();

    sweeping = true;
    unsigned long startTime = millis();
    char buf[32];

    while (sweeping) {
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        unsigned long uptime = (millis() - startTime) / 1000;

        u8g2.clearBuffer();

        // Title
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "AI Companion Robot");
        u8g2.drawLine(0, 12, 128, 12);

        if (!isnan(t) && !isnan(h)) {
            // Temperature
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 26, "Temp:");
            u8g2.setFont(u8g2_font_10x20_tr);
            snprintf(buf, sizeof(buf), "%.1fC", t);
            u8g2.drawStr(40, 28, buf);

            // Humidity
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 44, "Hum:");
            u8g2.setFont(u8g2_font_10x20_tr);
            snprintf(buf, sizeof(buf), "%.1f%%", h);
            u8g2.drawStr(40, 46, buf);
        } else {
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 28, "DHT11: reading...");
        }

        // Servo + uptime
        u8g2.setFont(u8g2_font_6x10_tr);
        snprintf(buf, sizeof(buf), "Pan:%.0f Tilt:%.0f %lus",
                 panAngle, tiltAngle, uptime);
        u8g2.drawStr(0, 62, buf);

        u8g2.sendBuffer();

        // Check for stop
        for (int i = 0; i < 10; i++) {
            delay(200);
            if (Serial.available()) {
                char c = Serial.read();
                if (c == 's' || c == 'S') {
                    sweeping = false;
                    break;
                }
            }
        }
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(30, 35, "Stopped.");
    u8g2.sendBuffer();
    Serial.println("===== OLED Live stopped =====\n");
}

void initMic() {
    if (micReady) return;

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

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) == ESP_OK &&
        i2s_set_pin(I2S_PORT, &pin_config) == ESP_OK) {
        micReady = true;
        Serial.println("  I2S microphone initialized OK");
    } else {
        Serial.println("  FAIL - I2S init failed");
    }
}

void testMicrophone() {
    Serial.println("\n===== TEST: INMP441 Microphone (GPIO 40/41/42) =====");
    Serial.println("Speak or clap near the mic! (type 's' to stop)\n");

    initMic();
    if (!micReady) {
        Serial.println("===== MIC TEST DONE =====\n");
        return;
    }

    sweeping = true;
    int32_t samples[I2S_BUF_LEN];
    size_t bytesRead = 0;
    int count = 0;

    // Debug: print raw sample values
    i2s_read(I2S_PORT, samples, sizeof(samples), &bytesRead, 1000 / portTICK_PERIOD_MS);
    Serial.printf("  DEBUG: %u bytes, raw[0]=%d raw[1]=%d raw[2]=%d raw[3]=%d\n",
                  bytesRead, samples[0], samples[1], samples[2], samples[3]);

    while (sweeping) {
        i2s_read(I2S_PORT, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
        int numSamples = bytesRead / sizeof(int32_t);

        if (numSamples == 0) continue;

        // Calculate RMS volume (use raw 32-bit values, scale after)
        int64_t sum = 0;
        int32_t maxVal = -2147483647;
        int32_t minVal = 2147483647;
        for (int i = 0; i < numSamples; i++) {
            int32_t s = samples[i] >> 8;  // Light shift only
            sum += (int64_t)s * s;
            if (samples[i] > maxVal) maxVal = samples[i];
            if (samples[i] < minVal) minVal = samples[i];
        }
        float rms = sqrt((float)sum / numSamples);
        int level = map(constrain((int)(rms / 100), 0, 3000), 0, 3000, 0, 30);

        // Print volume bar every 5th read
        count++;
        if (count % 5 == 0) {
            Serial.printf("  Vol: [");
            for (int i = 0; i < 30; i++) {
                Serial.print(i < level ? '#' : ' ');
            }
            Serial.printf("] RMS:%8.0f  Max:%d Min:%d\n", rms, maxVal, minVal);
        }

        // Check stop
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 's' || c == 'S') sweeping = false;
        }
    }

    i2s_driver_uninstall(I2S_PORT);
    micReady = false;
    Serial.println("===== MIC TEST DONE =====\n");
}

void initSpeaker() {
    if (spkReady) return;

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = I2S_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = SPK_BCLK,
        .ws_io_num = SPK_LRC,
        .data_out_num = SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    if (i2s_driver_install(SPK_PORT, &i2s_config, 0, NULL) == ESP_OK &&
        i2s_set_pin(SPK_PORT, &pin_config) == ESP_OK) {
        spkReady = true;
        Serial.println("  I2S speaker initialized OK");
    } else {
        Serial.println("  FAIL - I2S speaker init failed");
    }
}

void testSpeaker() {
    Serial.println("\n===== TEST: MAX98357A Speaker (GPIO 41/42/1) =====");
    Serial.println("Playing test tones...\n");

    initSpeaker();
    if (!spkReady) {
        Serial.println("===== SPEAKER TEST DONE =====\n");
        return;
    }

    // Generate and play sine wave tones
    int16_t samples[I2S_BUF_LEN];
    size_t bytesWritten;

    // Play 3 different tones
    int freqs[] = {440, 880, 1320};  // A4, A5, E6
    const char* names[] = {"440Hz (A4)", "880Hz (A5)", "1320Hz (E6)"};

    for (int f = 0; f < 3; f++) {
        Serial.printf("  Playing %s...\n", names[f]);
        float samplesPerCycle = (float)I2S_SAMPLE_RATE / freqs[f];

        // Play for ~0.5 second
        int totalSamples = I2S_SAMPLE_RATE / 2;
        int sent = 0;

        while (sent < totalSamples) {
            int count = min(I2S_BUF_LEN, totalSamples - sent);
            for (int i = 0; i < count; i++) {
                float angle = 2.0 * PI * (sent + i) / samplesPerCycle;
                samples[i] = (int16_t)(sin(angle) * 8000);  // Volume ~25%
            }
            i2s_write(SPK_PORT, samples, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
            sent += count;
        }
        delay(200);
    }

    // Play ascending beep pattern
    Serial.println("  Playing beep pattern...");
    for (int freq = 200; freq <= 2000; freq += 100) {
        float samplesPerCycle = (float)I2S_SAMPLE_RATE / freq;
        int count = I2S_SAMPLE_RATE / 20;  // 50ms per tone
        int sent = 0;
        while (sent < count) {
            int n = min(I2S_BUF_LEN, count - sent);
            for (int i = 0; i < n; i++) {
                float angle = 2.0 * PI * (sent + i) / samplesPerCycle;
                samples[i] = (int16_t)(sin(angle) * 6000);
            }
            i2s_write(SPK_PORT, samples, n * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
            sent += n;
        }
    }

    // Silence
    memset(samples, 0, sizeof(samples));
    i2s_write(SPK_PORT, samples, sizeof(samples), &bytesWritten, portMAX_DELAY);

    Serial.println("\n  Did you hear the tones?");
    Serial.println("  If not: check speaker wires on + and - terminals");

    i2s_driver_uninstall(SPK_PORT);
    spkReady = false;
    Serial.println("===== SPEAKER TEST DONE =====\n");
}

void micLoopback() {
    Serial.println("\n===== Mic → Speaker Loopback (type 's' to stop) =====");
    Serial.println("Speak into mic, hear yourself from speaker!\n");

    initMic();
    initSpeaker();
    if (!micReady || !spkReady) {
        Serial.println("  FAIL - mic or speaker not ready");
        return;
    }

    sweeping = true;
    int32_t micBuf[256];
    int16_t spkBuf[256];
    size_t bytesRead, bytesWritten;

    while (sweeping) {
        // Read from mic (32-bit)
        i2s_read(I2S_PORT, micBuf, sizeof(micBuf), &bytesRead, portMAX_DELAY);
        int numSamples = bytesRead / sizeof(int32_t);

        // Convert 32-bit mic data to 16-bit speaker data
        for (int i = 0; i < numSamples; i++) {
            spkBuf[i] = (int16_t)(micBuf[i] >> 14);
        }

        // Write to speaker
        i2s_write(SPK_PORT, spkBuf, numSamples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);

        if (Serial.available()) {
            char c = Serial.read();
            if (c == 's' || c == 'S') sweeping = false;
        }
    }

    i2s_driver_uninstall(I2S_PORT);
    i2s_driver_uninstall(SPK_PORT);
    micReady = false;
    spkReady = false;
    Serial.println("===== Loopback stopped =====\n");
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
    testOLED();
    testMicrophone();

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
    Serial.println("  8 = OLED display test");
    Serial.println("  9 = OLED live sensor");
    Serial.println("  m = INMP441 microphone");
    Serial.println("  p = MAX98357A speaker");
    Serial.println("  l = Mic→Speaker loopback");
    Serial.println("  s = Stop all");
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
            case '8': testOLED(); printMenu(); break;
            case '9': oledLiveSensor(); printMenu(); break;
            case 'm':
            case 'M': testMicrophone(); printMenu(); break;
            case 'p':
            case 'P': testSpeaker(); printMenu(); break;
            case 'l':
            case 'L': micLoopback(); printMenu(); break;
            case 's':
            case 'S': sweeping = false; break;
            default: break;
        }
    }
}
