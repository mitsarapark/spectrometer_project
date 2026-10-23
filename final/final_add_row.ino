#include <esp_camera.h>
#include <Arduino_GFX_Library.h>
#include "TCA9554.h"
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "img_converters.h"

// ============================================================
// DISPLAY CONFIGURATION (Your Working Setup)
// ============================================================
#define LCD_QSPI_CS   12
#define LCD_QSPI_CLK  5
#define LCD_QSPI_D0   1
#define LCD_QSPI_D1   2
#define LCD_QSPI_D2   3
#define LCD_QSPI_D3   4
#define ROTATION      0
#define GFX_BL        6

TCA9554 TCA(0x20);

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_QSPI_CS, LCD_QSPI_CLK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
Arduino_GFX *g = new Arduino_AXS15231B(bus, -1 /* RST */, 0 /* rotation */, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, g, 0, 0, ROTATION);

// ============================================================
// RGB565 COLOR DEFINES
// ============================================================
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define YELLOW      0xFFE0
#define CYAN        0x07FF
#define MAGENTA     0xF81F
#define ORANGE      0xFD20
#define PURPLE      0x8010
#define PINK        0xFE19
#define BROWN       0xA145
#define DARKGREY    0x4A69
#define LIGHTGREY   0xC618
#define DARKGREEN   0x0320
#define DARKBLUE    0x0010
#define MAROON      0x7800
#define OLIVE       0x7BE0
#define NAVY        0x000F
#define TEAL        0x0410
#define SILVER      0xC618
#define GOLD        0xFEA0

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// ============================================================
// CAMERA PIN DEFINITIONS (ESP32-S3-Touch-LCD-3.5B)
// ============================================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     38
#define SIOD_GPIO_NUM      8
#define SIOC_GPIO_NUM      7
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
#define FRAME_WIDTH       640
#define FRAME_HEIGHT      480
#define PIXEL_FORMAT      PIXFORMAT_RGB565

#define CENTER_COL        (FRAME_WIDTH / 2)
#define CENTER_ROW        (FRAME_WIDTH / 2)
#define COL_AVG_RANGE     10
#define TEMPORAL_FRAMES   4
#define SMOOTH_WINDOW     7

// Manual exposure & gain (tune these for your light source)
#define MANUAL_EXPOSURE   300
#define MANUAL_GAIN       0

// Display layout
#define DISP_WIDTH        320
#define DISP_HEIGHT       480
#define GRAPH_TOP         50
#define GRAPH_HEIGHT      260
#define GRAPH_BOTTOM      (GRAPH_TOP + GRAPH_HEIGHT)
#define GRAPH_LEFT        55
#define GRAPH_RIGHT       (DISP_WIDTH - 10)
#define GRAPH_WIDTH       (GRAPH_RIGHT - GRAPH_LEFT)
#define COLORBAR_Y        330
#define COLORBAR_HEIGHT   25
#define TEXT_Y            370

// Wavelength calibration
float wavelengthStart = 380.0;
float wavelengthEnd   = 750.0;

// Buffers
bool darkFrameCaptured = false;
uint16_t darkFrame[FRAME_HEIGHT];
float spectrumRaw[FRAME_HEIGHT];
float spectrumSmooth[FRAME_HEIGHT];
float spectrumTemporal[TEMPORAL_FRAMES][FRAME_HEIGHT];
uint8_t temporalIndex = 0;

// ============================================================
// WEB SERVER CONFIGURATION
// ============================================================
const char* ssid     = "ITIM-project";
const char* password = "itimsarapark";
WebServer server(80);

// Shared data for web endpoint (updated in loop, read by handler)
float webPeakWL   = 0;
float webMaxVal   = 0;
float webFPS      = 0;

// ============================================================
// HTML WEB PAGE (stored in PROGMEM)
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-S3 Spectrometer</title>
<style>
body { background: #0a0a0f; color: #e0e0e0; font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; }
h1 { color: #00d4ff; margin-bottom: 8px; }
.sub { color: #666; font-size: 14px; margin-bottom: 20px; }
.container { max-width: 700px; margin: 0 auto; }
.info { display: flex; gap: 12px; margin-bottom: 20px; flex-wrap: wrap; }
.box { background: #151520; padding: 15px; border-radius: 8px; flex: 1; min-width: 120px; border-left: 3px solid #ffaa00; }
.box h3 { margin: 0 0 8px 0; color: #ffaa00; font-size: 13px; text-transform: uppercase; }
.box div { font-size: 22px; color: #fff; font-weight: bold; }
#spectrumCanvas { background: #000; border: 1px solid #333; width: 100%; height: 400px; display: block; margin-bottom: 20px; border-radius: 4px; }
.cam-box { background: #151520; padding: 15px; border-radius: 8px; }
.cam-box h3 { margin: 0 0 10px 0; color: #00ff88; }
#camImg { width: 100%; max-width: 640px; border: 1px solid #333; display: block; border-radius: 4px; background: #000; }
.status { color: #666; font-size: 12px; margin-top: 10px; }
</style>
</head>
<body>
<div class="container">
<h1>🔬 ESP32-S3 Spectrometer</h1>
<div class="sub">Live Spectrum + Web Camera | <span id="conn">Connecting...</span></div>
<div class="info">
  <div class="box"><h3>Peak Wavelength</h3><div id="peak">-- nm</div></div>
  <div class="box"><h3>Max Intensity</h3><div id="max">-- / 255</div></div>
  <div class="box"><h3>FPS</h3><div id="fps">--</div></div>
</div>
<canvas id="spectrumCanvas" width="640" height="480"></canvas>
<div class="cam-box">
  <h3>📷 Camera Snapshot</h3>
  <img id="camImg" src="/capture" alt="Camera">
  <div class="status">Auto-refreshes every 3 seconds. Click image to refresh manually.</div>
</div>
</div>
<script>
const canvas = document.getElementById('spectrumCanvas');
const ctx = canvas.getContext('2d');

function wavelengthToRGB(wl) {
    let r=0,g=0,b=0;
    if(wl>=380&&wl<440){r=-(wl-440)/(440-380);g=0;b=1;}
    else if(wl>=440&&wl<490){r=0;g=(wl-440)/(490-440);b=1;}
    else if(wl>=490&&wl<510){r=0;g=1;b=-(wl-510)/(510-490);}
    else if(wl>=510&&wl<580){r=(wl-510)/(580-510);g=1;b=0;}
    else if(wl>=580&&wl<645){r=1;g=-(wl-645)/(645-580);b=0;}
    else if(wl>=645&&wl<=780){r=1;g=0;b=0;}
    let f=1;
    if(wl>=380&&wl<420)f=0.3+0.7*(wl-380)/(420-380);
    else if(wl>=700&&wl<=780)f=0.3+0.7*(780-wl)/(780-700);
    return `rgb(${Math.floor(r*f*255)},${Math.floor(g*f*255)},${Math.floor(b*f*255)})`;
}

function drawSpectrum(data) {
    ctx.fillStyle = '#000';
    ctx.fillRect(0,0,640,480);
    
    // Axes
    ctx.strokeStyle = '#444';
    ctx.lineWidth = 1;
    // Horizontal grid
    for(let i=0;i<=5;i++){
        let y=480-(i*480/5);
        ctx.beginPath();ctx.moveTo(50,y);ctx.lineTo(640,y);ctx.stroke();
    }
    // Vertical grid
    for(let i=0;i<=5;i++){
        let x=50+(i*590/5);
        ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,480);ctx.stroke();
    }
    
    // Border
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.strokeRect(50,0,590,480);
    
    // Wavelength labels (Y-axis)
    ctx.fillStyle = '#888';
    ctx.font = '12px Arial';
    ctx.textAlign = 'right';
    for(let nm=400;nm<=750;nm+=50){
        let frac=(nm-380)/(750-380);
        let y=480-frac*480;
        ctx.fillText(nm+'nm',45,y+4);
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1;
        ctx.beginPath();ctx.moveTo(45,y);ctx.lineTo(50,y);ctx.stroke();
    }
    ctx.fillText('nm',35,15);
    
    // Intensity labels (X-axis)
    ctx.textAlign = 'center';
    for(let i=0;i<=5;i++){
        let val=Math.round(i*255/5);
        let x=50+(i*590/5);
        ctx.fillText(val,x,470);
        ctx.beginPath();ctx.moveTo(x,480);ctx.lineTo(x,475);ctx.stroke();
    }
    ctx.fillText('Intensity (0-255)',345,470);
    
    // Spectrum fill
    for(let i=0;i<data.length;i++){
        let frac=i/data.length;
        let y=480-frac*480;
        let intensity=Math.min(255,Math.max(0,data[i]));
        let x=50+(intensity/255)*590;
        let wl=380+frac*(750-380);
        ctx.strokeStyle=wavelengthToRGB(wl);
        ctx.lineWidth=2;
        ctx.beginPath();ctx.moveTo(50,y);ctx.lineTo(x,y);ctx.stroke();
    }
    
    // White outline
    ctx.strokeStyle='#fff';
    ctx.lineWidth=1.5;
    ctx.beginPath();
    for(let i=0;i<data.length;i++){
        let frac=i/data.length;
        let y=480-frac*480;
        let intensity=Math.min(255,Math.max(0,data[i]));
        let x=50+(intensity/255)*590;
        if(i==0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
}

async function update() {
    try {
        const r=await fetch('/data');
        const j=await r.json();
        document.getElementById('peak').textContent=j.peak.toFixed(0)+' nm';
        document.getElementById('max').textContent=j.max.toFixed(0)+' / 255';
        document.getElementById('fps').textContent=j.fps.toFixed(1);
        document.getElementById('conn').textContent='Connected';
        document.getElementById('conn').style.color='#00ff88';
        drawSpectrum(j.spectrum);
    }catch(e){
        document.getElementById('conn').textContent='Disconnected';
        document.getElementById('conn').style.color='#ff4444';
    }
}

function updateCam() {
    document.getElementById('camImg').src='/capture?t='+Date.now();
}

document.getElementById('camImg').addEventListener('click',updateCam);
setInterval(update,500);
setInterval(updateCam,3000);
update();
</script>
</body></html>
)rawliteral";

// ============================================================
// WAVELENGTH TO RGB
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
// CAMERA INITIALIZATION (MANUAL / FIXED SETTINGS)
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

    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_VGA;
    config.pixel_format = PIXEL_FORMAT;
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

    // === ALL AUTO FEATURES DISABLED FOR SPECTROMETER ===
    s->set_exposure_ctrl(s, 0);
    s->set_gain_ctrl(s, 0);
    s->set_aec2(s, 0);
    s->set_awb_gain(s, 0);
    s->set_wb_mode(s, 0);

    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, -2);
    s->set_sharpness(s, -2);
    s->set_denoise(s, 0);

    s->set_aec_value(s, MANUAL_EXPOSURE);
    s->set_agc_gain(s, MANUAL_GAIN);

    s->set_bpc(s, 0);
    s->set_wpc(s, 0);
    s->set_lenc(s, 0);

    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);

    Serial.println("Camera initialized (MANUAL mode)");
    return true;
}

// ============================================================
// DISPLAY INITIALIZATION
// ============================================================
bool initDisplay() {
    Wire.begin(21, 22);
    TCA.begin();
    TCA.pinMode1(1, OUTPUT);
    TCA.write1(1, 1);
    delay(10);
    TCA.write1(1, 0);
    delay(10);
    TCA.write1(1, 1);
    delay(200);

    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
        return false;
    }

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    gfx->fillScreen(BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 10);
    gfx->println("Spectrometer v1.0");
    gfx->setCursor(10, 25);
    gfx->println("Initializing...");
    gfx->flush();

    Serial.println("Display initialized");
    return true;
}

// ============================================================
// SPECTRUM EXTRACTION
// ============================================================
void extractSpectrum(camera_fb_t *fb, float *spectrum) {
    int colStart = CENTER_ROW - COL_AVG_RANGE;
    int colEnd = CENTER_ROW + COL_AVG_RANGE;
    if (colStart < 0) colStart = 0;
    if (colEnd >= FRAME_WIDTH) colEnd = FRAME_WIDTH - 1;
    int numCols = colEnd - colStart + 1;

    for (int row = 0; row < FRAME_HEIGHT; row++) {
        uint32_t rSum = 0, gSum = 0, bSum = 0;
        for (int col = colStart; col <= colEnd; col++) {
            int pixelIndex = row * FRAME_WIDTH + col;
            uint16_t pixel = ((uint16_t*)fb->buf)[pixelIndex];

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
        float intensity = 0.299f * (rSum / numCols)
                        + 0.587f * (gSum / numCols)
                        + 0.114f * (bSum / numCols);
        spectrum[row] = intensity;
    }
}

// ============================================================
// NOISE REDUCTION
// ============================================================
void captureDarkFrame() {
    gfx->fillScreen(BLACK);
    gfx->setCursor(10, DISP_HEIGHT / 2);
    gfx->setTextColor(YELLOW);
    gfx->println("Capturing dark frame...");
    gfx->println("Cover the spectrometer slit!");
    gfx->flush();
    delay(2000);

    float tempSpectrum[FRAME_HEIGHT];
    uint32_t darkSum[FRAME_HEIGHT] = {0};

    for (int f = 0; f < 8; f++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;
        extractSpectrum(fb, tempSpectrum);
        esp_camera_fb_return(fb);
        for (int i = 0; i < FRAME_HEIGHT; i++) darkSum[i] += (uint32_t)tempSpectrum[i];
        delay(100);
    }
    for (int i = 0; i < FRAME_HEIGHT; i++) darkFrame[i] = darkSum[i] / 8;

    darkFrameCaptured = true;
    gfx->fillScreen(BLACK);
    gfx->setCursor(10, DISP_HEIGHT / 2);
    gfx->setTextColor(GREEN);
    gfx->println("Dark frame captured!");
    gfx->flush();
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
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        spectrumTemporal[temporalIndex][i] = currentSpectrum[i];
    }
    temporalIndex = (temporalIndex + 1) % TEMPORAL_FRAMES;

    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float sum = 0;
        for (int f = 0; f < TEMPORAL_FRAMES; f++) {
            sum += spectrumTemporal[f][i];
        }
        output[i] = sum / TEMPORAL_FRAMES;
    }
}

// ============================================================
// LCD DISPLAY DRAWING (Fixed 0-255 scale)
// ============================================================
void drawAxes() {
    gfx->drawRect(GRAPH_LEFT, GRAPH_TOP, GRAPH_WIDTH, GRAPH_HEIGHT, WHITE);

    // --- Horizontal axis: Intensity 0-255 ---
    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);

    for (int val = 0; val <= 255; val += 51) {
        int x = GRAPH_LEFT + (val * GRAPH_WIDTH) / 255;
        gfx->drawLine(x, GRAPH_BOTTOM, x, GRAPH_BOTTOM + 5, WHITE);
        gfx->drawLine(x, GRAPH_TOP, x, GRAPH_BOTTOM, DARKGREY);
        gfx->setCursor(x - 10, GRAPH_BOTTOM + 8);
        gfx->printf("%d", val);
    }
    gfx->setCursor(GRAPH_LEFT + GRAPH_WIDTH / 2 - 40, GRAPH_BOTTOM + 20);
    gfx->print("Intensity (0-255)");

    // --- Vertical axis: Wavelength nm ---
    for (int nm = 400; nm <= 750; nm += 50) {
        float frac = (nm - wavelengthStart) / (wavelengthEnd - wavelengthStart);
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);
        if (y < GRAPH_TOP || y > GRAPH_BOTTOM) continue;

        gfx->drawLine(GRAPH_LEFT - 5, y, GRAPH_LEFT, y, WHITE);
        gfx->drawLine(GRAPH_LEFT, y, GRAPH_RIGHT, y, DARKGREY);
        gfx->setCursor(2, y - 4);
        gfx->printf("%d", nm);
    }
    gfx->setCursor(8, GRAPH_TOP - 12);
    gfx->print("nm");
}

void drawSpectrum(float *spectrum) {
    const float MAX_INTENSITY = 255.0f;

    gfx->fillRect(GRAPH_LEFT + 1, GRAPH_TOP + 1, GRAPH_WIDTH - 1, GRAPH_HEIGHT - 1, BLACK);

    // Fill area with wavelength colors
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float frac = (float)i / FRAME_HEIGHT;
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);

        float intensity = spectrum[i];
        if (intensity < 0) intensity = 0;
        if (intensity > 255) intensity = 255;

        int x = GRAPH_LEFT + (int)((intensity / MAX_INTENSITY) * GRAPH_WIDTH);
        if (x > GRAPH_RIGHT) x = GRAPH_RIGHT;

        uint8_t r, g, b;
        float wl = wavelengthStart + frac * (wavelengthEnd - wavelengthStart);
        wavelengthToRGB(wl, r, g, b);
        uint16_t color = RGB565(r, g, b);

        if (x > GRAPH_LEFT) {
            gfx->drawLine(GRAPH_LEFT, y, x, y, color);
        }
    }

    // White outline on top
    int prevX = -1, prevY = -1;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        float frac = (float)i / FRAME_HEIGHT;
        int y = GRAPH_BOTTOM - (int)(frac * GRAPH_HEIGHT);

        float intensity = spectrum[i];
        if (intensity < 0) intensity = 0;
        if (intensity > 255) intensity = 255;

        int x = GRAPH_LEFT + (int)((intensity / MAX_INTENSITY) * GRAPH_WIDTH);
        if (x > GRAPH_RIGHT) x = GRAPH_RIGHT;

        if (prevX >= 0) {
            gfx->drawLine(prevX, prevY, x, y, WHITE);
        }
        prevX = x;
        prevY = y;
    }
}

void drawColorBar() {
    int barW = GRAPH_WIDTH;
    for (int x = 0; x < barW; x++) {
        float frac = (float)x / barW;
        float wl = wavelengthStart + frac * (wavelengthEnd - wavelengthStart);
        uint8_t r, g, b;
        wavelengthToRGB(wl, r, g, b);
        uint16_t color = RGB565(r, g, b);
        gfx->fillRect(GRAPH_LEFT + x, COLORBAR_Y, 2, COLORBAR_HEIGHT, color);
    }
    gfx->drawRect(GRAPH_LEFT, COLORBAR_Y, barW, COLORBAR_HEIGHT, WHITE);
}

void drawInfo(float *spectrum, float maxVal, float fps) {
    int peakIdx = 0;
    float peakVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrum[i] > peakVal) {
            peakVal = spectrum[i];
            peakIdx = i;
        }
    }
    float peakWL = wavelengthStart + ((float)peakIdx / FRAME_HEIGHT) * (wavelengthEnd - wavelengthStart);

    gfx->fillRect(0, TEXT_Y, DISP_WIDTH, DISP_HEIGHT - TEXT_Y, BLACK);

    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, TEXT_Y);
    gfx->printf("Peak: %.0f nm", peakWL);

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, TEXT_Y + 25);
    gfx->printf("Max Int: %.0f/255  FPS: %.1f", maxVal, fps);

    if (maxVal >= 255.0f) {
        gfx->setTextColor(RED);
        gfx->setCursor(10, TEXT_Y + 38);
        gfx->print("WARNING: CLIPPING!");
    }

    gfx->setCursor(10, TEXT_Y + 55);
    gfx->setTextColor(YELLOW);
    gfx->printf("Cols:%d  TempAvg:%d  Smooth:%d", COL_AVG_RANGE * 2 + 1, TEMPORAL_FRAMES, SMOOTH_WINDOW);

    gfx->setCursor(10, TEXT_Y + 70);
    if (darkFrameCaptured) {
        gfx->setTextColor(GREEN);
        gfx->print("Dark frame: ON");
    } else {
        gfx->setTextColor(RED);
        gfx->print("Dark frame: OFF (press BOOT)");
    }
}

// ============================================================
// WEB SERVER HANDLERS
// ============================================================
void handleRoot() {
    server.send(200, "text/html", index_html);
}

void handleData() {
    // Downsample spectrum to 80 points for web transfer
    String json = "{\"peak\":";
    json += String(webPeakWL, 1);
    json += ",\"max\":";
    json += String(webMaxVal, 1);
    json += ",\"fps\":";
    json += String(webFPS, 1);
    json += ",\"spectrum\":[";

    int webPoints = 80;
    for (int i = 0; i < webPoints; i++) {
        int idx = i * FRAME_HEIGHT / webPoints;
        json += String((int)spectrumSmooth[idx]);
        if (i < webPoints - 1) json += ",";
    }
    json += "]}";

    server.send(200, "application/json", json);
}

void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }

    uint8_t* jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &jpg_buf, &jpg_len);
    esp_camera_fb_return(fb);

    if (!converted || !jpg_buf) {
        server.send(500, "text/plain", "JPEG conversion failed");
        return;
    }

    server.setContentLength(jpg_len);
    server.send(200, "image/jpeg", "");
    server.client().write(jpg_buf, jpg_len);
    free(jpg_buf);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("\n=== ESP32-S3 Spectrometer + Web ===");

    // Init LCD
    if (!initDisplay()) {
        Serial.println("Display init failed!");
    }

    // Init Camera
    if (!initCamera()) {
        gfx->setCursor(10, 50);
        gfx->setTextColor(RED);
        gfx->println("CAMERA FAILED!");
        gfx->flush();
        while (1) delay(100);
    }

    // Init temporal buffer
    for (int f = 0; f < TEMPORAL_FRAMES; f++) {
        for (int i = 0; i < FRAME_HEIGHT; i++) {
            spectrumTemporal[f][i] = 0;
        }
    }

    // Init WiFi AP
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("WiFi AP started: ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(IP);

    // Show IP on LCD
    gfx->fillScreen(BLACK);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, 200);
    gfx->println("WiFi AP Started");
    gfx->setCursor(10, 220);
    gfx->print("SSID: ");
    gfx->println(ssid);
    gfx->setCursor(10, 240);
    gfx->print("IP: ");
    gfx->println(IP.toString());
    gfx->flush();
    delay(3000);

    // Setup web server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/capture", HTTP_GET, handleCapture);
    server.begin();
    Serial.println("Web server started on port 80");

    // Initial LCD screen
    gfx->fillScreen(BLACK);
    drawAxes();
    drawColorBar();
    gfx->flush();

    Serial.println("Setup complete. Starting capture...");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    // Always handle web clients (non-blocking)
    server.handleClient();

    static unsigned long lastFrameTime = 0;
    static int frameCount = 0;
    static unsigned long fpsTimer = 0;
    static float fps = 0;

    unsigned long now = millis();

    // Check BOOT button for dark frame
    if (digitalRead(0) == LOW) {
        delay(200);
        captureDarkFrame();
        gfx->fillScreen(BLACK);
        drawAxes();
        drawColorBar();
        gfx->flush();
        lastFrameTime = millis(); // reset timer
        return;
    }

    // Spectrum update every 1 second
    if (now - lastFrameTime < 1000) {
        return;
    }
    lastFrameTime = now;

    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Frame capture failed");
        return;
    }

    extractSpectrum(fb, spectrumRaw);
    esp_camera_fb_return(fb);

    // Noise reduction pipeline
    applyDarkFrameSubtraction(spectrumRaw);
    applyTemporalAverage(spectrumRaw, spectrumSmooth);

    float tempSmooth[FRAME_HEIGHT];
    applyMovingAverage(spectrumSmooth, tempSmooth, FRAME_HEIGHT, SMOOTH_WINDOW);
    memcpy(spectrumSmooth, tempSmooth, sizeof(tempSmooth));

    // Find max for display
    float maxVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrumSmooth[i] > maxVal) maxVal = spectrumSmooth[i];
    }

    // Find peak wavelength
    int peakIdx = 0;
    float peakVal = 0;
    for (int i = 0; i < FRAME_HEIGHT; i++) {
        if (spectrumSmooth[i] > peakVal) {
            peakVal = spectrumSmooth[i];
            peakIdx = i;
        }
    }
    float peakWL = wavelengthStart + ((float)peakIdx / FRAME_HEIGHT) * (wavelengthEnd - wavelengthStart);

    // Update web shared data
    webPeakWL = peakWL;
    webMaxVal = maxVal;
    webFPS = fps;

    // Update LCD
    drawSpectrum(spectrumSmooth);
    drawColorBar();

    // Calculate FPS
    frameCount++;
    if (now - fpsTimer >= 1000) {
        fps = frameCount * 1000.0f / (now - fpsTimer);
        frameCount = 0;
        fpsTimer = now;
    }

    drawInfo(spectrumSmooth, maxVal, fps);
    gfx->flush();
}