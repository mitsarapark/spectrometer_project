
code = r'''/*
  ESP32-S3-Touch-LCD-3.5B Spectrometer with OV5640
  
  Hardware:
  - Waveshare ESP32-S3-Touch-LCD-3.5B (320x480 IPS LCD, AXS15231B driver)
  - OV5640 5MP camera module (DVP interface)
  
  Features:
  - Captures frames from OV5640 in RGB565 format
  - Extracts spectrum from middle column(s) of image
  - Temporal averaging (multi-frame) for noise reduction
  - Spatial averaging (multi-column) for noise reduction
  - Moving average smoothing on spectrum data
  - Dark frame subtraction
  - Real-time spectrum graph display on built-in LCD
  - Peak wavelength detection
  - ~1 FPS update rate
  
  Libraries required:
  - esp32-camera (included with ESP32 Arduino core)
  - Arduino_GFX (by moononournation) OR GFX_Library_for_Arduino
  - TCA9554 (Waveshare IO expander, if using their display examples)
  
  Board settings (Arduino IDE):
  - Board: "ESP32S3 Dev Module"
  - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)" or "Custom"
  - PSRAM: "OPI PSRAM"
  - USB CDC On Boot: "Enabled" (for Serial output)
*/

#include <esp_camera.h>

// ============================================================
// DISPLAY CONFIGURATION
// ============================================================
// The ESP32-S3-Touch-LCD-3.5B uses an AXS15231B display over QSPI.
// If you have the Waveshare demo package, use their display init.
// Otherwise, use Arduino_GFX with the pin config below.

#include <Arduino_GFX_Library.h>

/* Arduino_GFX display initialization for AXS15231B QSPI */
// QSPI pins for ESP32-S3-Touch-LCD-3.5B (from Waveshare schematic)
#define GFX_BL 46  // Backlight pin (adjust if needed)

Arduino_DataBus *bus = new Arduino_SWSPI(
    39 /* DC */, 40 /* CS */,
    41 /* SCK */, 42 /* MOSI */, -1 /* MISO */);
    
// NOTE: For the actual 3.5B board, Waveshare uses a specific QSPI setup.
// The above is a placeholder. Use the Waveshare-provided display init
// from their example package for guaranteed compatibility.
// Download examples from: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5B/

Arduino_GFX *gfx = new Arduino_AXS15231B(bus, 45 /* RST */, 0 /* rotation */, true /* IPS */);

// If using Waveshare's official library with TCA9554, replace the above with:
// #include "WavesharePanel.h"
// WavesharePanel panel;

// ============================================================
// CAMERA PIN DEFINITIONS (ESP32-S3-Touch-LCD-3.5B)
// ============================================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     38
#define SIOD_GPIO_NUM      8   // SDA
#define SIOC_GPIO_NUM      7   // SCL
#define Y9_GPIO_NUM       21
#define Y8_GPIO_NUM       39
#define Y7_GPIO_NUM       40
#define Y6_GPIO_NUM       42
#define Y5_GPIO_NUM       46
#define Y4_GPIO_NUM       48
#define Y3_GPIO_NUM       47
#define Y2_GPIO_NUM       45
#define VSYNC_GPIO_NUM    17
#define HREF_GPIO_NUM     18
#define PCLK_GPIO_NUM     41

// ============================================================
// SPECTROMETER SETTINGS
// ============================================================
// Camera frame settings
#define FRAME_WIDTH       640
#define FRAME_HEIGHT      480
#define PIXEL_FORMAT      PIXFORMAT_RGB565

// Spectrum extraction: average columns around center
#define CENTER_COL        (FRAME_WIDTH / 2)
#define COL_AVG_RANGE     10   // Average from CENTER-COL_AVG_RANGE to CENTER+COL_AVG_RANGE

// Temporal averaging: number of frames to average
#define TEMPORAL_FRAMES   4

// Moving average window for spectrum smoothing
#define SMOOTH_WINDOW     7

// Dark frame subtraction
bool darkFrameCaptured = false;
uint16_t darkFrame[FRAME_HEIGHT];

// Spectrum buffers
float spectrumRaw[FRAME_HEIGHT];
float spectrumSmooth[FRAME_HEIGHT];
float spectrumTemporal[TEMPORAL_FRAMES][FRAME_HEIGHT];
uint8_t temporalIndex = 0;

// Wavelength calibration (approximate linear mapping)
// These values MUST be calibrated for your optical setup!
// Use known spectral lines (e.g., fluorescent lamp: 404.7, 435.8, 546.1, 578nm)
float wavelengthStart = 380.0;  // nm at pixel 0
float wavelengthEnd   = 750.0;  // nm at pixel FRAME_HEIGHT-1

// Display layout
#define DISP_WIDTH        320
#define DISP_HEIGHT       480
#define GRAPH_TOP         60
#define GRAPH_HEIGHT      250
#define GRAPH_BOTTOM      (GRAPH_TOP + GRAPH_HEIGHT)
#define COLORBAR_Y        330
#define COLORBAR_HEIGHT   30
#define TEXT_Y            380

// ============================================================
// WAVELENGTH TO RGB (for color bar display)
// Based on Dan Bruton's approximation
// ============================================================
void wavelengthToRGB(float wavelength, uint8_t &r, uint8_t &g, uint8_t &b) {
    float rr = 0, gg = 0, bb = 0;
    
    if (wavelength >= 380 && wavelength < 440) {
        rr = -(wavelength - 440) / (440 - 380);
        gg = 0.0;
        bb = 1.0;
    } else if (wavelength >= 440 && wavelength < 490) {
        rr = 0.0;
        gg = (wavelength - 440) / (490 - 440);
        bb = 1.0;
    } else if (wavelength >= 490 && wavelength < 510) {
        rr = 0.0;
        gg = 1.0;
        bb = -(wavelength - 510) / (510 - 490);
    } else if (wavelength >= 510 && wavelength < 580) {
        rr = (wavelength - 510) / (580 - 510);
        gg = 1.0;
        bb = 0.0;
    } else if (wavelength >= 580 && wavelength < 645) {
        rr = 1.0;
        gg = -(wavelength - 645) / (645 - 580);
        bb = 0.0;
    } else if (wavelength >= 645 && wavelength <= 780) {
        rr = 1.0;
        gg = 0.0;
        bb = 0.0;
    }
    
    // Intensity falloff near UV and IR
    float factor = 1.0;
    if (wavelength >= 380 && wavelength < 420) {
        factor = 0.3 + 0.7 * (wavelength - 380) / (420 - 380);
    } else if (wavelength >= 700 && wavelength <= 780) {
        factor = 0.3 + 0.7 * (780 - wavelength) / (780 - 700);
    }
    
    r = (uint8_t)(rr * factor * 255);
    g = (uint8_t)(gg * factor * 255);
    b = (uint8_t)(bb * factor * 255);
}

// ============================================================
// CAMERA INITIALIZATION
// ============================================================
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    
    config.xclk_freq_hz = 20000000;       // 20MHz XCLK
    config.frame_size = FRAMESIZE_VGA;    // 640x480 (use FRAMESIZE_QVGA for 320x240 if too slow)
    config.pixel_format = PIXEL_FORMAT;   // RGB565 = 2 bytes/pixel
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 0;
    config.fb_count = 2;
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        Serial.println("Failed to get camera sensor");
        return false;
    }
    
    // OV5640 specific settings
    if (s->id.PID == OV5640_PID) {
        s->set_brightness(s, 0);      // -2 to 2
        s->set_contrast(s, 0);        // -2 to 2
        s->set_saturation(s, 0);      // -2 to 2
        s->set_sharpness(s, 0);       // Auto
        s->set_denoise(s, 1);         // Enable denoise
        s->set_gainceiling(s, GAINCEILING_16X);
        s->set_aec2(s, true);         // Auto exposure
        s->set_awb_gain(s, true);     // Auto white balance
        s->set_wb_mode(s, 0);         // Auto
        s->set_exposure_ctrl(s, true);
        s->set_gain_ctrl(s, true);
        s->set_bpc(s, 1);             // Black pixel correction
        s->set_wpc(s, 1);             // White pixel correction
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
    }
    
    Serial.println("Camera initialized successfully");
    Serial.printf("Sensor PID: 0x%04X\n", s->id.PID);
    return true;
}

// ============================================================
// DISPLAY INITIALIZATION
// ============================================================
bool initDisplay() {
    if (!gfx->begin()) {
        Serial.println("Display init failed!");
        return false;
    }
    
    gfx->fillScreen(BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 10);
    gfx->println("Spectrometer v1.0");
    gfx->setCursor(10, 25);
    gfx->println("Initializing...");
    
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);  // Turn on backlight
    
    Serial.println("Display initialized");
    return true;
}

// ============================================================
// SPECTRUM EXTRACTION
// ============================================================
void extractSpectrum(camera_fb_t *fb, float *spectrum) {
    // fb->buf contains RGB565 data: 2 bytes per pixel
    // Layout: row-major, each row has FRAME_WIDTH pixels
    // RGB565 format: RRRRRGGGGGGBBBBB (5-6-5 bits)
    
    int colStart = CENTER_COL - COL_AVG_RANGE;
    int colEnd = CENTER_COL + COL_AVG_RANGE;
    if (colStart < 0) colStart = 0;
    if (colEnd >= FRAME_WIDTH) colEnd = FRAME_WIDTH - 1;
    int numCols = colEnd - colStart + 1;
    
    for (int row = 0; row < FRAME_HEIGHT; row++) {
        uint32_t rSum = 0, gSum = 0, bSum = 0;
        
        for (int col = colStart; col <= colEnd; col++) {
            int pixelIndex = row * FRAME_WIDTH + col;
            uint16_t pixel = ((uint16_t*)fb->buf)[pixelIndex];
            
            // Extract RGB from RGB565
            uint8_t r = (pixel >> 11) & 0x1F;  
            uint8_t g = (pixel >> 5) & 0x3F;   
            uint8_t b = pixel & 0x1F;          
            
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            rSum += r;
            gSum += g;
            bSum += b;
        }
        
        // Average across columns
        float rAvg = rSum / numCols;
        float gAvg = gSum / numCols;
        float bAvg = bSum / numCols;
        
        // Convert to intensity (luminance)
        float intensity = 0.299f * rAvg + 0.587f * gAvg + 0.114f * bAvg;
        
        spectrum[row] = intensity;
    }
}

// ============================================================
// NOISE REDUCTION
// ============================================================
void captureDarkFrame() {
    // Cover the camera/slit and call this to capture a dark reference
    gfx->fillScreen(BLACK);
    gfx->setCursor(10, DISP_HEIGHT/2);
    gfx->setTextColor(YELLOW);
    gfx->println("Capturing dark frame...");
    gfx->println("Cover the spectrometer slit!");
    
    delay(2000);
    
    float tempSpectrum[FRAME_HEIGHT];
    uint32_t darkSum[FRAME_HEIGHT] = {0};
    
    for (int f = 0; f < 8; f++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;
        extractSpectrum(fb, tempSpectrum);
        esp_camera_fb_return(fb);
        
        for (int i = 0; i < FRAME_HEIGHT; i++) {
            darkSum[i] += (uint32_t)tempSpectrum[i];
        }
        delay(100);
    }
    
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        darkFrame[i] = darkSum[i] / 8;
    }
    
    darkFrameCaptured = true;
    gfx->fillScreen(BLACK);
    gfx->setCursor(10, DISP_HEIGHT/2);
    gfx->setTextColor(GREEN);
    gfx->println("Dark frame captured!");
    delay(1000);
}

void applyDarkFrameSubtraction(float *spectrum) {
    if (!darkFrameCaptured) return;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        spectrum[i] -= darkFrame[i];
        if (spectrum[i] < 0) spectrum[i] = 0;
    }
}

void applyMovingAverage(float *input, float *output, int len, int window) {
    int half = window / 2;
    for (int i = 0; i < len; i++) {
        float sum = 0;
        int count = 0;
        for (int j = -half; j <= half; j++) {
            int idx = i + j;
            if (idx >= 0 && idx < len) {
                sum += input[idx];
                count++;
            }
        }
        output[i] = sum / count;
    }
}

void applyTemporalAverage(float *currentSpectrum, float *output) {
    // Store current frame
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        spectrumTemporal[temporalIndex][i] = currentSpectrum[i];
    }
    temporalIndex = (temporalIndex + 1) % TEMPORAL_FRAMES;
    
    // Average all stored frames
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float sum = 0;
        for (int f = 0; f < TEMPORAL_FRAMES; f++) {
            sum += spectrumTemporal[f][i];
        }
        output[i] = sum / TEMPORAL_FRAMES;
    }
}

// ============================================================
// DISPLAY DRAWING
// ============================================================
void drawGrid() {
    gfx->drawRect(0, GRAPH_TOP, DISP_WIDTH, GRAPH_HEIGHT, DARKGREY);
    
    // Horizontal grid lines
    for (int i = 1; i < 5; i++) {
        int y = GRAPH_TOP + (GRAPH_HEIGHT * i) / 5;
        gfx->drawLine(0, y, DISP_WIDTH, y, DARKGREY);
    }
    
    // Wavelength labels
    gfx->setTextSize(1);
    gfx->setTextColor(LIGHTGREY);
    for (int nm = 400; nm <= 750; nm += 50) {
        float frac = (nm - wavelengthStart) / (wavelengthEnd - wavelengthStart);
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);
        if (y >= GRAPH_TOP && y <= GRAPH_BOTTOM) {
            gfx->setCursor(2, y - 4);
            gfx->printf("%dnm", nm);
            gfx->drawLine(35, y, DISP_WIDTH, y, DARKGREY);
        }
    }
}

void drawSpectrum(float *spectrum) {
    // Find max for normalization
    float maxVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrum[i] > maxVal) maxVal = spectrum[i];
    }
    if (maxVal < 1) maxVal = 1;
    
    // Clear graph area
    gfx->fillRect(0, GRAPH_TOP, DISP_WIDTH, GRAPH_HEIGHT, BLACK);
    drawGrid();
    
    // Draw spectrum line
    int prevX = -1, prevY = -1;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float frac = (float)i / FRAME_HEIGHT;
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);
        int x = 40 + (int)((spectrum[i] / maxVal) * (DISP_WIDTH - 45));
        if (x > DISP_WIDTH - 1) x = DISP_WIDTH - 1;
        
        if (prevX >= 0) {
            gfx->drawLine(prevX, prevY, x, y, GREEN);
        }
        prevX = x;
        prevY = y;
    }
    
    // Fill area under curve
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float frac = (float)i / FRAME_HEIGHT;
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);
        int x = 40 + (int)((spectrum[i] / maxVal) * (DISP_WIDTH - 45));
        
        uint8_t r, g, b;
        float wl = wavelengthStart + frac * (wavelengthEnd - wavelengthStart);
        wavelengthToRGB(wl, r, g, b);
        uint16_t color = gfx->color565(r, g, b);
        
        if (x > 40) {
            gfx->drawLine(40, y, x, y, color);
        }
    }
}

void drawColorBar() {
    for (int x = 40; x < DISP_WIDTH - 5; x++) {
        float frac = (float)(x - 40) / (DISP_WIDTH - 45);
        float wl = wavelengthStart + frac * (wavelengthEnd - wavelengthStart);
        uint8_t r, g, b;
        wavelengthToRGB(wl, r, g, b);
        uint16_t color = gfx->color565(r, g, b);
        gfx->fillRect(x, COLORBAR_Y, 2, COLORBAR_HEIGHT, color);
    }
    gfx->drawRect(40, COLORBAR_Y, DISP_WIDTH - 45, COLORBAR_HEIGHT, WHITE);
}

void drawInfo(float *spectrum, float maxVal, float fps) {
    // Find peak wavelength
    int peakIdx = 0;
    float peakVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrum[i] > peakVal) {
            peakVal = spectrum[i];
            peakIdx = i;
        }
    }
    float peakWL = wavelengthStart + ((float)peakIdx / FRAME_HEIGHT) * (wavelengthEnd - wavelengthStart);
    
    // Clear text area
    gfx->fillRect(0, TEXT_Y, DISP_WIDTH, DISP_HEIGHT - TEXT_Y, BLACK);
    
    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, TEXT_Y);
    gfx->printf("Peak: %.0f nm", peakWL);
    
    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, TEXT_Y + 25);
    gfx->printf("Intensity: %.0f/255  FPS: %.1f", maxVal, fps);
    
    gfx->setCursor(10, TEXT_Y + 40);
    gfx->setTextColor(YELLOW);
    gfx->printf("Cols: %d  TempAvg: %d  Smooth: %d", COL_AVG_RANGE*2+1, TEMPORAL_FRAMES, SMOOTH_WINDOW);
    
    gfx->setCursor(10, TEXT_Y + 55);
    if (darkFrameCaptured) {
        gfx->setTextColor(GREEN);
        gfx->print("Dark frame: ON");
    } else {
        gfx->setTextColor(RED);
        gfx->print("Dark frame: OFF (press BOOT)");
    }
}

// ============================================================
// SETUP & LOOP
// ============================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);  // Wait for Serial
    
    Serial.println("\n=== ESP32-S3 Spectrometer ===");
    
    if (!initDisplay()) {
        Serial.println("Display init failed!");
    }
    
    if (!initCamera()) {
        gfx->setCursor(10, 50);
        gfx->setTextColor(RED);
        gfx->println("CAMERA FAILED!");
        while (1) delay(100);
    }
    
    // Initialize temporal buffer
    for (int f = 0; f < TEMPORAL_FRAMES; f++) {
        for (int i = 0; i < FRAME_HEIGHT; i++) {
            spectrumTemporal[f][i] = 0;
        }
    }
    
    gfx->fillScreen(BLACK);
    drawGrid();
    drawColorBar();
    
    Serial.println("Setup complete. Starting capture...");
}

void loop() {
    static unsigned long lastFrameTime = 0;
    static int frameCount = 0;
    static unsigned long fpsTimer = 0;
    static float fps = 0;
    
    // Check for BOOT button press to capture dark frame
    if (digitalRead(0) == LOW) {  // BOOT button is GPIO0
        delay(200);  // Debounce
        captureDarkFrame();
        gfx->fillScreen(BLACK);
        drawGrid();
        drawColorBar();
    }
    
    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Frame capture failed");
        delay(100);
        return;
    }
    
    // Extract spectrum from frame
    extractSpectrum(fb, spectrumRaw);
    esp_camera_fb_return(fb);
    
    // Apply noise reduction pipeline
    applyDarkFrameSubtraction(spectrumRaw);
    applyTemporalAverage(spectrumRaw, spectrumSmooth);
    
    // Additional spatial smoothing on the 1D spectrum
    float tempSmooth[FRAME_HEIGHT];
    applyMovingAverage(spectrumSmooth, tempSmooth, FRAME_HEIGHT, SMOOTH_WINDOW);
    memcpy(spectrumSmooth, tempSmooth, sizeof(tempSmooth));
    
    // Find max value for normalization
    float maxVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrumSmooth[i] > maxVal) maxVal = spectrumSmooth[i];
    }
    
    // Update display
    drawSpectrum(spectrumSmooth);
    drawColorBar();
    
    // Calculate FPS
    frameCount++;
    unsigned long now = millis();
    if (now - fpsTimer >= 1000) {
        fps = frameCount * 1000.0f / (now - fpsTimer);
        frameCount = 0;
        fpsTimer = now;
    }
    
    drawInfo(spectrumSmooth, maxVal, fps);
    
    // Target ~1 FPS (adjust as needed)
    unsigned long elapsed = millis() - lastFrameTime;
    if (elapsed < 1000) {
        delay(1000 - elapsed);
    }
    lastFrameTime = millis();
}
'''

with open('/mnt/agents/output/ESP32_S3_Spectrometer.ino', 'w') as f:
    f.write(code)

print("Code saved successfully!")
print(f"File size: {len(code)} characters")
