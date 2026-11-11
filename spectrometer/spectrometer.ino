#include <esp_camera.h>
#include <Arduino_GFX_Library.h>
#include "TCA9554.h"
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
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
// RGB565 COLOR DEFINES (raw hex - named constants not reliable here)
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

// I2C for the TCA9554 IO expander and the AXS15231B touch panel.
// On this board both sit on 8/7 - the same pads as the camera SCCB, NOT
// on 21/22 (21 is camera D7, so the touch IC never answered there).
#define I2C_SDA_GPIO       8
#define I2C_SCL_GPIO       7

// ============================================================
// SPECTROMETER SETTINGS
// ============================================================
#define FRAME_WIDTH       640
#define FRAME_HEIGHT      480
#define PIXEL_FORMAT      PIXFORMAT_RGB565

// Reading the CENTER COLUMN down every row (vertical spectrum).
// The grating in this build disperses the spectrum vertically, so the
// measured axis is the image row (0 = top .. FRAME_HEIGHT-1 = bottom).
#define CENTER_COL        (FRAME_WIDTH / 2)
#define COL_AVG_RANGE     10      // average this many columns left+right of CENTER_COL
#define SPECTRUM_LEN      FRAME_HEIGHT

// Set to 1 if the spectrum appears reversed on screen (red at the wrong
// end); this flips the row order without touching anything else.
#define SPECTRUM_INVERT   0

#define TEMPORAL_FRAMES   4
#define SMOOTH_WINDOW     7

// How often the spectrum is re-measured and the LCD is refreshed.
#define FRAME_INTERVAL_MS 300

// Manual exposure & gain (tune these for your light source)
#define MANUAL_EXPOSURE   300
#define MANUAL_GAIN       0

// Onboard BOOT button: short press = predict, long press = dark frame
#define DARKFRAME_BUTTON_PIN  0
#define DARKFRAME_HOLD_MS     1500

// Optional third button for axis calibration (spectrum tilt/offset).
// Set to the GPIO number once the button is wired; -1 disables it.
// /calibrate on the web page works without any button.
#define CAL_BUTTON_PIN        -1

// How the R/G/B channels are combined into the single displayed /
// recorded intensity value (see combineForDisplay()).
#define COMBINE_REC709        0
#define COMBINE_REC601        1
#define COMBINE_AVERAGE       2
#define COMBINE_MODE          COMBINE_REC709

// Two-point wavelength calibration.
// Default (uncalibrated) mapping spreads the whole sensor column over
// the visible range: row 0 = 380 nm, last row = 700 nm, so every graph,
// CSV column and model input is expressed in 380-700 nm.
// After measuring real reference lines (e.g. a fluorescent tube's
// 405 nm and 546 nm peaks) replace CAL_PIXEL_1/2 with those row indices;
// the axis then follows the real dispersion instead of this placeholder.
#define CAL_PIXEL_1           0
#define CAL_NM_1              380.0f
#define CAL_PIXEL_2           (SPECTRUM_LEN - 1)
#define CAL_NM_2              700.0f

// Hard limits used by the pipeline/UI: values outside this window are
// not part of the measurement.
#define SPEC_NM_MIN           380.0f
#define SPEC_NM_MAX           700.0f

// Touch panel (AXS15231B integrated touch controller on the shared
// Wire bus). Set TOUCH_ENABLE 0 if the controller is not detected.
#define TOUCH_ENABLE          1
#define TOUCH_I2C_ADDR        0x3B
#define TOUCH_POLL_MS         60

// Axis auto-tracking per frame. 0 = the yellow line only moves when a
// calibration is requested (button, serial 'c' or /calibrate); set to 1
// to let it follow the band continuously again.
#define AXIS_AUTO_DETECT      0

// Display layout (x=position, y=intensity)
#define DISP_WIDTH        320
#define DISP_HEIGHT       480
#define GRAPH_LEFT        35
#define GRAPH_RIGHT       (DISP_WIDTH - 10)
#define GRAPH_WIDTH       (GRAPH_RIGHT - GRAPH_LEFT)
#define GRAPH_TOP         50
#define GRAPH_HEIGHT      240
#define GRAPH_BOTTOM      (GRAPH_TOP + GRAPH_HEIGHT)
#define COLORBAR_Y        (GRAPH_BOTTOM + 25)
#define COLORBAR_HEIGHT   20
#define TEXT_Y            (COLORBAR_Y + 35)

// Display-only scale (NOT a data clamp - see spectrum_processing.h).
// Raw R/G/B channel values coming off an 8-bit sensor are 0-255 each,
// but after temporal+moving average they can still legitimately sit
// anywhere in that range; 300 gives a bit of headroom above 255 so a
// bright peak doesn't visually clip against the top of the graph.
// Tune this to your light source if peaks look cut off (see the
// on-screen warning that appears when a real value exceeds this).
#define DISPLAY_MAX_INTENSITY  300.0f

// The x-axis is wavelength in nm via the 2-point calibration above
// (placeholder until real reference lines are measured). Colors are
// cosmetic (see wavelengthToRGB()).

#include "model_weights.h"
#include "spectrum_processing.h"
#include "display.h"

// ============================================================
// WEB SERVER CONFIGURATION (AP mode)
// ============================================================
const char* ssid     = "ITIM-project";
const char* password = "itimsarapark";
WebServer server(80);
DNSServer dnsServer;
Preferences axisPrefs;
// Diagnostic listener: tells us when a browser tries HTTPS (port 443)
// instead of plain HTTP, which is a common reason "the page never loads".
WiFiServer httpsProbe(443);

#include "inference.h"
#include "dataset.h"
#include "touch.h"

// Give the Arduino loop task extra headroom when the core supports it
// (the spectrum pipeline, JPEG conversion and web handlers all run here).
// The large scratch buffers have been moved off the stack, so this is
// only a safety margin.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

// Timestamp of the last completed spectrum measurement. /record rejects
// requests when this is stale so a sample can't be saved with old data.
unsigned long lastSpectrumMs = 0;

// Shared data for web endpoint (updated in loop, read by handler)
float webPeakPos  = 0;   // peak position (pixel index), not wavelength
float webMaxVal   = 0;   // real (unclamped) max intensity value
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
#spectrumCanvas { background: #000; border: 1px solid #333; width: 100%; height: 300px; display: block; margin-bottom: 20px; border-radius: 4px; }
.cam-box { background: #151520; padding: 15px; border-radius: 8px; position: relative; }
.cam-box h3 { margin: 0 0 10px 0; color: #00ff88; }
.cam-wrap { position: relative; max-width: 640px; }
#camImg { width: 100%; border: 1px solid #333; display: block; border-radius: 4px; background: #000; }
.status { color: #666; font-size: 12px; margin-top: 10px; }
.row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.row input { background: #0a0a0f; color: #fff; border: 1px solid #444; border-radius: 4px; padding: 6px 8px; width: 110px; }
.row button { background: #1c3b52; color: #bde8ff; border: 1px solid #2f6b93; border-radius: 4px; padding: 7px 12px; cursor: pointer; font-size: 13px; }
.row button:hover { background: #265172; }
</style>
</head>
<body>
<div class="container">
<h1>ðŸ”¬ ESP32-S3 Spectrometer</h1>
<div class="sub">Live Spectrum + Web Camera | <span id="conn">Connecting...</span></div>
<div class="info">
  <div class="box"><h3>Peak Wavelength</h3><div id="peak">-- nm</div></div>
  <div class="box"><h3>Max Intensity</h3><div id="max">--</div></div>
  <div class="box"><h3>Concentration</h3><div id="conc">--</div></div>
  <div class="box"><h3>FPS</h3><div id="fps">--</div></div>
</div>
<canvas id="spectrumCanvas" width="640" height="300"></canvas>
<div class="cam-box">
  <h3>ðŸ“· Camera Snapshot</h3>
  <div class="cam-wrap">
    <img id="camImg" src="/capture" alt="Camera">
  </div>
  <div class="status" id="axisStatus">Auto-refreshes every 1 second. Yellow line = measured center, thin edges = averaged columns.</div>
</div>
<div class="cam-box" style="margin-top:20px">
  <h3>Axis trim (tilt / shift)</h3>
  <div class="status" id="trimStatus">a=-- b=--</div>
  <div class="row">
    <label>Tilt a</label>
    <button class="trim" data-da="-0.005" data-db="0">-0.005</button>
    <button class="trim" data-da="-0.001" data-db="0">-0.001</button>
    <button class="trim" data-da="0.001" data-db="0">+0.001</button>
    <button class="trim" data-da="0.005" data-db="0">+0.005</button>
  </div>
  <div class="row" style="margin-top:8px">
    <label>Shift b</label>
    <button class="trim" data-da="0" data-db="-10">-10 px</button>
    <button class="trim" data-da="0" data-db="-1">-1 px</button>
    <button class="trim" data-da="0" data-db="1">+1 px</button>
    <button class="trim" data-da="0" data-db="10">+10 px</button>
  </div>
  <div class="row" style="margin-top:8px">
    <label>Set a</label>
    <input id="axisAInput" type="number" step="0.001" value="0" style="width:90px">
    <label>b</label>
    <input id="axisBInput" type="number" step="1" value="0" style="width:90px">
    <button id="axisSetBtn">Apply</button>
  </div>
  <div class="row" style="margin-top:8px">
    <button id="trimResetBtn">Reset to center</button>
    <button id="trimSaveBtn">Save now</button>
  </div>
  <div class="status">Each click moves the line; check the yellow line in the snapshot. Edits are stored in flash 1.5 s after the last click.</div>
</div>
<div class="cam-box" style="margin-top:20px">
  <h3>ðŸ§ª Dataset</h3>
  <div class="row">
    <label for="concInput">Concentration</label>
    <input id="concInput" type="number" step="0.01" value="0">
    <button id="recordBtn">Record sample</button>
    <button id="calBtn">Calibrate axis</button>
    <button id="autoBtn">Auto align</button>
    <a id="dlLink" href="/dataset.csv"><button>Download CSV</button></a>
    <button id="clearBtn">Clear</button>
  </div>
  <div class="status" id="dsStatus">samples: --</div>
</div>
</div>
<script>
const canvas = document.getElementById('spectrumCanvas');
const ctx = canvas.getContext('2d');

// Cosmetic-only wavelength -> color mapping (380..780 nm), used for the
// graph line and the color gradient. Values outside the visible range are
// clamped by the firmware's calibration, so no extra handling here.
function nmToRGB(wl) {
    if (wl < 380) wl = 380;
    if (wl > 780) wl = 780;
    let r=0,g=0,b=0;
    if(wl<440){r=-(wl-440)/(440-380);b=1;}
    else if(wl<490){g=(wl-440)/(490-440);b=1;}
    else if(wl<510){g=1;b=-(wl-510)/(510-490);}
    else if(wl<580){r=(wl-510)/(580-510);g=1;}
    else if(wl<645){r=1;g=-(wl-645)/(645-580);}
    else {r=1;}
    let f=1;
    if(wl<420)f=0.3+0.7*(wl-380)/(420-380);
    else if(wl>700)f=0.3+0.7*(780-wl)/(780-700);
    return `rgb(${Math.floor(r*f*255)},${Math.floor(g*f*255)},${Math.floor(b*f*255)})`;
}

// data = real (unclamped) intensity values. scale = the display scale
// the firmware is currently using (same one shown on the LCD) so the
// web graph and LCD graph read consistently. A per-frame value above
// `scale` is clamped only for the on-screen pixel position - never in
// the underlying data. nmStart/nmEnd come from the on-device 2-point
// wavelength calibration.
function drawSpectrum(data, scale, nmStart, nmEnd) {
    const W = canvas.width, H = canvas.height;
    const left = 45, right = W - 10, top = 10, bottom = H - 30;
    const gw = right - left, gh = bottom - top;

    ctx.fillStyle = '#000';
    ctx.fillRect(0,0,W,H);

    ctx.strokeStyle = '#333';
    ctx.lineWidth = 1;
    for(let i=0;i<=5;i++){
        let y = bottom - (i*gh/5);
        ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();
    }
    for(let i=0;i<=8;i++){
        let x = left + (i*gw/8);
        ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,bottom);ctx.stroke();
    }

    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(left,top,gw,gh);

    // Y axis labels (intensity, 0..scale)
    ctx.fillStyle = '#888';
    ctx.font = '11px Arial';
    ctx.textAlign = 'right';
    for(let i=0;i<=5;i++){
        let val = Math.round(i*scale/5);
        let y = bottom - (i*gh/5);
        ctx.fillText(val, left-5, y+4);
    }

    // X axis labels (wavelength, nm)
    ctx.textAlign = 'center';
    for(let i=0;i<=8;i++){
        let x = left + (i*gw/8);
        let nm = nmStart + (i/8)*(nmEnd-nmStart);
        ctx.fillText(Math.round(nm), x, bottom+15);
    }
    ctx.fillText('Wavelength (nm)', left+gw/2, bottom+27);

    let prevX=-1, prevY=-1;
    ctx.lineWidth = 2;
    for(let i=0;i<data.length;i++){
        let frac = i/(data.length-1);
        let x = left + frac*gw;
        let normalized = Math.min(1, Math.max(0, data[i]/scale));
        let y = bottom - normalized*gh;
        if(prevX>=0){
            ctx.strokeStyle = nmToRGB(nmStart + frac*(nmEnd-nmStart));
            ctx.beginPath();ctx.moveTo(prevX,prevY);ctx.lineTo(x,y);ctx.stroke();
        }
        prevX=x; prevY=y;
    }
}

async function update() {
    try {
        const r=await fetch('/data');
        const j=await r.json();
        document.getElementById('peak').textContent=j.peakNm.toFixed(0)+' nm';
        document.getElementById('max').textContent=j.max.toFixed(1)+' (scale 0-'+j.scale+')';
        document.getElementById('fps').textContent=j.fps.toFixed(1);
        document.getElementById('conc').textContent = j.modelReady
            ? (j.hasPred ? j.pred.toFixed(2)+' '+j.modelUnit : 'not run yet')
            : 'no model';
        document.getElementById('dsStatus').textContent = 'samples: '+j.samples;
        document.getElementById('conn').textContent='Connected';
        document.getElementById('conn').style.color='#00ff88';
        document.getElementById('axisStatus').textContent =
            'Axis a=' + j.axisA.toFixed(4) + '  b=' + j.axisB.toFixed(1) + ' px ' +
            (j.axisLocked ? '(locked)' : '(not calibrated)') +
            ' | yellow line = measured center';
        document.getElementById('trimStatus').textContent =
            'a=' + j.axisA.toFixed(4) + '  b=' + j.axisB.toFixed(1) + ' px (locked)';
        const aIn = document.getElementById('axisAInput');
        const bIn = document.getElementById('axisBInput');
        if (document.activeElement !== aIn) aIn.value = j.axisA.toFixed(4);
        if (document.activeElement !== bIn) bIn.value = j.axisB.toFixed(1);
        drawSpectrum(j.spectrum, j.scale, j.nmStart, j.nmEnd);
    }catch(e){
        document.getElementById('conn').textContent='Disconnected';
        document.getElementById('conn').style.color='#ff4444';
    }
}

async function recordSample() {
    const c = document.getElementById('concInput').value;
    const el = document.getElementById('dsStatus');
    try {
        const r = await fetch('/record?conc='+encodeURIComponent(c));
        const j = await r.json();
        el.textContent = j.ok ? ('recorded, samples: '+j.count) : ('error: '+j.err);
        el.style.color = j.ok ? '#00ff88' : '#ff4444';
    } catch (e) {
        el.textContent = 'error: request failed';
        el.style.color = '#ff4444';
    }
}

async function clearDataset() {
    if (!confirm('Delete all recorded samples?')) return;
    const el = document.getElementById('dsStatus');
    try {
        const r = await fetch('/dataset/clear');
        const j = await r.json();
        el.textContent = j.ok ? 'dataset cleared' : 'error: '+j.err;
        el.style.color = j.ok ? '#00ff88' : '#ff4444';
    } catch (e) {
        el.textContent = 'error: request failed';
        el.style.color = '#ff4444';
    }
}

async function calibrateAxis() {
    const el = document.getElementById('dsStatus');
    el.textContent = 'calibrating axis...';
    el.style.color = '#ffaa00';
    try {
        const r = await fetch('/calibrate');
        const j = await r.json();
        el.textContent = j.ok
            ? ('axis a=' + j.a.toFixed(4) + '  b=' + j.b.toFixed(1) + ' px (locked)')
            : 'axis calibration failed (no spectrum)';
        el.style.color = j.ok ? '#00ff88' : '#ff4444';
    } catch (e) {
        el.textContent = 'axis calibration failed';
        el.style.color = '#ff4444';
    }
}

async function autoAlign() {
    const el = document.getElementById('dsStatus');
    el.textContent = 'auto-aligning axis...';
    el.style.color = '#ffaa00';
    try {
        const r = await fetch('/axis/auto');
        const j = await r.json();
        el.textContent = j.ok
            ? ('axis a=' + j.a.toFixed(4) + '  b=' + j.b.toFixed(1) + ' px (locked)')
            : 'no spectrum detected';
        el.style.color = j.ok ? '#00ff88' : '#ff4444';
    } catch (e) {
        el.textContent = 'auto align failed';
        el.style.color = '#ff4444';
    }
}

document.getElementById('recordBtn').addEventListener('click', recordSample);
document.getElementById('calBtn').addEventListener('click', calibrateAxis);
document.getElementById('autoBtn').addEventListener('click', autoAlign);
document.getElementById('clearBtn').addEventListener('click', clearDataset);

// Manual axis trim: the device applies the delta, locks the line and
// persists it after a short idle period, so a burst of clicks only costs
// one flash write. The snapshot (refreshed every second) shows the result.
async function axisCmd(url) {
    const el = document.getElementById('trimStatus');
    try {
        const r = await fetch(url);
        const j = await r.json();
        showAxis(j);
    } catch (e) {
        el.textContent = 'axis trim failed';
        el.style.color = '#ff4444';
    }
}

function showAxis(j) {
    const el = document.getElementById('trimStatus');
    el.textContent = 'a=' + j.a.toFixed(4) + '  b=' + j.b.toFixed(1) + ' px (locked)';
    el.style.color = '#00ff88';
}

document.querySelectorAll('button.trim').forEach(function (b) {
    b.addEventListener('click', function () {
        axisCmd('/axis/nudge?da=' + b.dataset.da + '&db=' + b.dataset.db);
    });
});
document.getElementById('trimResetBtn').addEventListener('click', function () { axisCmd('/axis/reset'); });
document.getElementById('trimSaveBtn').addEventListener('click', function () { axisCmd('/axis/save'); });
document.getElementById('axisSetBtn').addEventListener('click', function () {
    const a = document.getElementById('axisAInput').value;
    const b = document.getElementById('axisBInput').value;
    axisCmd('/axis/set?a=' + encodeURIComponent(a) + '&b=' + encodeURIComponent(b));
});

function updateCam() {
    document.getElementById('camImg').src='/capture?t='+Date.now();
}

setInterval(update,500);
setInterval(updateCam,1000);
update();
</script>
</body></html>
)rawliteral";

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
// WEB SERVER HANDLERS
// ============================================================
void handleRoot() {
    Serial.printf("GET / from %s\n", server.client().remoteIP().toString().c_str());
    server.send(200, "text/html", index_html);
}

void handleData() {
    // Downsample spectrumCombined (REAL, unclamped values) to 80 points
    // for web transfer. "scale" tells the client what range the
    // firmware is currently displaying at on the LCD, so both stay
    // consistent - the values themselves are never clamped.
    String json = "{\"peak\":";
    json += String(webPeakPos, 0);
    json += ",\"peakNm\":";
    json += String(pixelToNm(webPeakPos), 1);
    json += ",\"max\":";
    json += String(webMaxVal, 1);
    json += ",\"scale\":";
    json += String((int)DISPLAY_MAX_INTENSITY);
    json += ",\"nmStart\":";
    json += String(pixelToNm(0), 1);
    json += ",\"nmEnd\":";
    json += String(pixelToNm(SPECTRUM_LEN - 1), 1);
    json += ",\"axisA\":";
    json += String(specSlope, 5);
    json += ",\"axisB\":";
    json += String(specIntercept, 2);
    json += ",\"axisOn\":";
    json += axisDetected ? "true" : "false";
    json += ",\"axisLocked\":";
    json += axisLocked ? "true" : "false";
    json += ",\"col\":";
    json += String(CENTER_COL);
    json += ",\"colRange\":";
    json += String(COL_AVG_RANGE);
    json += ",\"frameWidth\":";
    json += String(FRAME_WIDTH);
    json += ",\"frameHeight\":";
    json += String(FRAME_HEIGHT);
    json += ",\"modelReady\":";
    json += MODEL_READY ? "true" : "false";
    json += ",\"hasPred\":";
    json += hasPrediction ? "true" : "false";
    json += ",\"pred\":";
    json += String(lastPrediction, 3);
    json += ",\"modelUnit\":\"";
    json += MODEL_UNIT;
    json += "\",\"samples\":";
    json += String(datasetSampleCount);
    json += ",\"fps\":";
    json += String(webFPS, 1);
    json += ",\"spectrum\":[";

    int webPoints = 80;
    for (int i = 0; i < webPoints; i++) {
        int idx = i * SPECTRUM_LEN / webPoints;
        json += String(spectrumCombined[idx], 1);
        if (i < webPoints - 1) json += ",";
    }
    json += "]}";

    server.send(200, "application/json", json);
}

// Burn the measurement line into the frame served by /capture. The old
// web overlay was drawn from /data, i.e. from a different frame, so it
// always lagged the image; now the line and the spectrum are the same
// snapshot.
static void drawAxisOnFrame(camera_fb_t *fb) {
    uint16_t *pix = (uint16_t *)fb->buf;
    const int w = fb->width;
    const int h = fb->height;
    const uint16_t lineColor = RGB565(255, 221, 0);   // yellow
    const uint16_t edgeColor = RGB565(110, 100, 30);  // sampled band edges

    for (int y = 0; y < h; y++) {
        int c = (int)(specSlope * (float)y + specIntercept + 0.5f);
        for (int dx = -1; dx <= 1; dx++) {
            int x = c + dx;
            if (x >= 0 && x < w) pix[(size_t)y * w + x] = lineColor;
        }
        int e1 = c - COL_AVG_RANGE;
        int e2 = c + COL_AVG_RANGE;
        if (e1 >= 0 && e1 < w) pix[(size_t)y * w + e1] = edgeColor;
        if (e2 >= 0 && e2 < w) pix[(size_t)y * w + e2] = edgeColor;
    }
}

void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }

    drawAxisOnFrame(fb);

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
// DATASET ENDPOINTS
// ============================================================
void handleRecord() {
    if (!server.hasArg("conc")) {
        server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing conc\"}");
        return;
    }
    if (lastSpectrumMs == 0) {
        server.send(409, "application/json", "{\"ok\":false,\"err\":\"no spectrum yet\"}");
        return;
    }
    if (millis() - lastSpectrumMs > 3000) {
        server.send(409, "application/json", "{\"ok\":false,\"err\":\"spectrum is stale\"}");
        return;
    }
    float conc = server.arg("conc").toFloat();
    if (!appendSample(conc)) {
        server.send(500, "application/json", "{\"ok\":false,\"err\":\"write failed\"}");
        return;
    }
    String out = "{\"ok\":true,\"count\":";
    out += String(datasetSampleCount);
    out += "}";
    server.send(200, "application/json", out);
}

void handleDatasetDownload() {
    if (datasetFS == nullptr) {
        server.send(503, "text/plain", "dataset storage unavailable");
        return;
    }
    fs::File f = datasetFS->open(DATASET_PATH, FILE_READ);
    if (!f) {
        server.send(404, "text/plain", "dataset not found");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"dataset.csv\"");
    server.streamFile(f, "text/csv");
    f.close();
}

void handleDatasetClear() {
    clearDataset();
    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------- Axis calibration / status endpoints ----------------
bool runAxisCalibration(); // defined with the button handlers below
bool autoAlignAxis();

// The calibrated line lives in NVS so it survives a reboot. It is only
// rewritten by a calibration - never by the per-frame pipeline.
void loadAxisCalibration() {
    axisPrefs.begin("axis", true);
    if (axisPrefs.isKey("a") && axisPrefs.isKey("b")) {
        specSlope = axisPrefs.getFloat("a", 0.0f);
        specIntercept = axisPrefs.getFloat("b", (float)CENTER_COL);
        axisDetected = true;
        axisLocked = true;
        clampSpectrumAxis();
        Serial.printf("Axis loaded from NVS: a=%.4f b=%.1f (locked)\n",
                      specSlope, specIntercept);
    }
    axisPrefs.end();
}

void saveAxisCalibration() {
    axisPrefs.begin("axis", false);
    axisPrefs.putFloat("a", specSlope);
    axisPrefs.putFloat("b", specIntercept);
    axisPrefs.end();
}

void handleCalibrate() {
    bool ok = runAxisCalibration();
    String out = "{\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"locked\":";
    out += axisLocked ? "true" : "false";
    out += ",\"a\":";
    out += String(specSlope, 5);
    out += ",\"b\":";
    out += String(specIntercept, 2);
    out += "}";
    server.send(200, "application/json", out);
}

void handleAxis() {
    String out = "{\"a\":";
    out += String(specSlope, 5);
    out += ",\"b\":";
    out += String(specIntercept, 2);
    out += ",\"detected\":";
    out += axisDetected ? "true" : "false";
    out += ",\"locked\":";
    out += axisLocked ? "true" : "false";
    out += "}";
    server.send(200, "application/json", out);
}

// One-shot detection used by /axis/auto and serial 'a': fit the current
// frame, lock the result and store it. Never runs on its own.
bool autoAlignAxis() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;

    float a = 0.0f, b = 0.0f;
    bool ok = fitSpectrumAxis(fb, a, b);
    esp_camera_fb_return(fb);

    if (!ok) {
        Serial.println("Auto axis: no spectrum detected");
        return false;
    }

    specSlope = a;
    specIntercept = b;
    axisDetected = true;
    axisLocked = true;
    clampSpectrumAxis();
    saveAxisCalibration();
    Serial.printf("Auto axis: a=%.4f b=%.1f (locked)\n", specSlope, specIntercept);
    return true;
}

// Drop the manual lock and let the per-frame tracker take over again
// (only useful when AXIS_AUTO_DETECT is 1).
void handleAxisAuto() {
    bool ok = autoAlignAxis();
    String out = "{\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"locked\":";
    out += axisLocked ? "true" : "false";
    out += ",\"a\":";
    out += String(specSlope, 5);
    out += ",\"b\":";
    out += String(specIntercept, 2);
    out += "}";
    server.send(200, "application/json", out);
}

// ---------------- Manual axis trim (web) ----------------
// Detection can land a few pixels (or a fraction of a degree) off when the
// band is weak or partly clipped. These endpoints move/lock the calibrated
// line by hand. Writes to NVS are deferred so a burst of clicks costs one
// flash write instead of one per request.
bool          axisSavePending = false;
unsigned long axisSaveAt      = 0;

static void sendAxisJson() {
    String out = "{\"ok\":true";
    out += ",\"a\":";
    out += String(specSlope, 5);
    out += ",\"b\":";
    out += String(specIntercept, 2);
    out += ",\"locked\":";
    out += axisLocked ? "true" : "false";
    out += "}";
    server.send(200, "application/json", out);
}

// Apply a delta, keep the line inside the sensor, lock it and queue a save.
static void axisTrimApply(float da, float db) {
    specSlope += da;
    specIntercept += db;
    if (specSlope > AXIS_MAX_SLOPE)  specSlope = AXIS_MAX_SLOPE;
    if (specSlope < -AXIS_MAX_SLOPE) specSlope = -AXIS_MAX_SLOPE;
    axisDetected = true;
    axisLocked   = true;
    clampSpectrumAxis();
    axisSavePending = true;
    axisSaveAt = millis();
    Serial.printf("Axis trim: a=%.4f b=%.1f\n", specSlope, specIntercept);
}

// /axis/nudge?da=<px-per-row>&db=<px>
void handleAxisNudge() {
    float da = server.hasArg("da") ? server.arg("da").toFloat() : 0.0f;
    float db = server.hasArg("db") ? server.arg("db").toFloat() : 0.0f;
    if (!isfinite(da) || !isfinite(db)) {
        server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad value\"}");
        return;
    }
    axisTrimApply(da, db);
    sendAxisJson();
}

// /axis/set?a=<a>&b=<b>  (absolute values)
void handleAxisSet() {
    float a = server.hasArg("a") ? server.arg("a").toFloat() : specSlope;
    float b = server.hasArg("b") ? server.arg("b").toFloat() : specIntercept;
    if (!isfinite(a) || !isfinite(b)) {
        server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad value\"}");
        return;
    }
    specSlope = a;
    specIntercept = b;
    axisTrimApply(0.0f, 0.0f);
    sendAxisJson();
}

// /axis/reset -> horizontal line through CENTER_COL
void handleAxisReset() {
    specSlope = 0.0f;
    specIntercept = (float)CENTER_COL;
    axisTrimApply(0.0f, 0.0f);
    sendAxisJson();
}

// /axis/save -> flush a pending trim immediately
void handleAxisSave() {
    saveAxisCalibration();
    axisSavePending = false;
    sendAxisJson();
}

// ============================================================
// SETUP
// ============================================================
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        Serial.printf("[ap] client connected (aid=%u)\n",
                      (unsigned)info.wifi_ap_staconnected.aid);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        Serial.println("[ap] client disconnected");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("\n=== ESP32-S3 Spectrometer + Web (AP mode, column-center, RGB-separate) ===");

    pinMode(DARKFRAME_BUTTON_PIN, INPUT_PULLUP); // BOOT button: HIGH = not pressed, LOW = pressed
#if CAL_BUTTON_PIN >= 0
    pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);
#endif

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

    resetTemporalBuffer();
    loadAxisCalibration();

    // Init WiFi AP
    WiFi.onEvent(onWiFiEvent);
    if (!WiFi.softAP(ssid, password)) {
        Serial.println("WiFi AP failed to start!");
    }
    WiFi.setSleep(false);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("WiFi AP started: ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(IP);
    Serial.printf("AP channel: %d\n", WiFi.channel());

    // Captive portal: resolve every hostname to this AP so phones/PCs pop
    // up the page automatically after joining the network. Typing
    // http://192.168.4.1 (or http://spectrometer.local) still works.
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.println("Captive portal DNS started");

    if (MDNS.begin("spectrometer")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://spectrometer.local");
    }

    // Web server must be listening BEFORE the boot splash delay,
    // otherwise a client that joins the AP right away gets
    // "connection refused" and no request ever reaches the board.
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/record", HTTP_GET, handleRecord);
    server.on("/dataset.csv", HTTP_GET, handleDatasetDownload);
    server.on("/dataset/clear", HTTP_GET, handleDatasetClear);
    server.on("/calibrate", HTTP_GET, handleCalibrate);
    server.on("/axis", HTTP_GET, handleAxis);
    server.on("/axis/auto", HTTP_GET, handleAxisAuto);
    server.on("/axis/nudge", HTTP_GET, handleAxisNudge);
    server.on("/axis/set", HTTP_GET, handleAxisSet);
    server.on("/axis/reset", HTTP_GET, handleAxisReset);
    server.on("/axis/save", HTTP_GET, handleAxisSave);
    server.on("/ping", HTTP_GET, []() {
        server.send(200, "text/plain", "pong");
    });
    // Anything else is treated as a captive-portal probe (Android's
    // /generate_204, Apple's /hotspot-detect.html, Windows NCSI, ...) and
    // redirected to the main page so the phone/PC opens it by itself.
    server.onNotFound([]() {
        String uri = server.uri();
        Serial.printf("PORTAL %s\n", uri.c_str());
        String target = String("http://") + WiFi.softAPIP().toString() + "/";
        server.sendHeader("Location", target, true);
        server.send(302, "text/plain", "");
    });
    server.begin();
    httpsProbe.begin();
    Serial.println("Web server started on port 80");
    Serial.printf("Open: http://%s/   (no port needed, HTTP/80)\n",
                  WiFi.softAPIP().toString().c_str());

    // Dataset storage + touch panel
    initDataset();
    initTouch();

    // Show IP on LCD (boot splash)
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
    gfx->setCursor(10, 258);
    gfx->setTextSize(1);
    gfx->println("http://192.168.4.1");
    gfx->flush();
    delay(1500);

    // Initial LCD screen
    gfx->fillScreen(BLACK);
    drawAxes();
    drawColorBar();
    drawPredictButton(false);
    gfx->flush();

    Serial.println("Setup complete. Starting capture...");
    Serial.println("Serial commands: c=axis calibrate (locks), d=dark frame, p=predict, a=resume auto axis");
}

// ============================================================
// BUTTON HANDLING (BOOT short/long press, on-screen touch button)
// ============================================================
unsigned long lastFrameTime = 0;
unsigned long popupUntil = 0;
float popupValue = 0.0f;
bool popupOk = false;

void triggerPrediction() {
    float v = 0.0f;
    bool ok = runPrediction(v);
    popupValue = v;
    popupOk = ok;
    popupUntil = millis() + 4000;
    Serial.printf("Predict: %s value=%.3f\n", ok ? "ok" : "not-ready", v);
}

static void sortFloats(float *v, int n) {
    for (int i = 1; i < n; i++) {
        float key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }
}

// Axis (tilt/offset) calibration: average the fitted center line over a
// short burst and lock it in. Point the spectrometer at a known, steady
// light source before running this. While locked, updateSpectrumAxis()
// stops tracking so the line only changes on the next calibration
// ('c' = 20-frame median, 'a' or /axis/auto = single-frame refit).
bool runAxisCalibration() {
    gfx->fillScreen(BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(10, DISP_HEIGHT / 2);
    gfx->println("Axis calibration...");
    gfx->println("Point at a steady light source");
    gfx->flush();

    const int N = 20;
    float slopes[N], intercepts[N];
    int m = 0;

    for (int i = 0; i < N; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            float a = 0.0f, b = 0.0f;
            if (fitSpectrumAxis(fb, a, b)) {
                slopes[m] = a;
                intercepts[m] = b;
                m++;
            }
            esp_camera_fb_return(fb);
        }
        delay(40);
    }

    gfx->fillScreen(BLACK);
    gfx->setTextSize(1);
    gfx->setCursor(10, DISP_HEIGHT / 2);

    if (m >= 5) {
        sortFloats(slopes, m);
        sortFloats(intercepts, m);
        specSlope = slopes[m / 2];
        specIntercept = intercepts[m / 2];
        axisDetected = true;
        axisLocked = true;   // freeze the line until the next calibration
        clampSpectrumAxis();
        saveAxisCalibration();
        gfx->setTextColor(GREEN);
        gfx->printf("Axis OK (locked)\n a = %+.4f\n b = %.1f px", specSlope, specIntercept);
        Serial.printf("Axis calib: a=%.4f b=%.1f (from %d frames, locked)\n",
                      specSlope, specIntercept, m);
    } else {
        gfx->setTextColor(RED);
        gfx->println("Axis calibration failed");
        gfx->println("(no spectrum detected)");
        Serial.printf("Axis calib failed (only %d valid frames)\n", m);
    }
    gfx->flush();
    delay(1500);

    gfx->fillScreen(BLACK);
    drawAxes();
    drawColorBar();
    drawPredictButton(false);
    gfx->flush();
    lastFrameTime = millis();
    return (m >= 5);
}

void runDarkFrame() {
    gfx->fillScreen(BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(10, DISP_HEIGHT / 2);
    gfx->println("Capturing dark frame...");
    gfx->println("Cover the spectrometer slit!");
    gfx->flush();
    delay(2000);

    captureDarkFrame(); // also resets the temporal buffer internally

    gfx->fillScreen(BLACK);
    gfx->setTextColor(GREEN);
    gfx->setCursor(10, DISP_HEIGHT / 2);
    gfx->println("Dark frame captured!");
    gfx->flush();
    delay(1000);

    gfx->fillScreen(BLACK);
    drawAxes();
    drawColorBar();
    drawPredictButton(false);
    gfx->flush();
    lastFrameTime = millis();
}

// BOOT button: short press = predict, long press (>= DARKFRAME_HOLD_MS)
// = dark frame. Acting on release keeps both actions unambiguous.
void handleBootButton() {
    static bool wasPressed = false;
    static unsigned long pressStart = 0;

    bool pressed = (digitalRead(DARKFRAME_BUTTON_PIN) == LOW);

    if (pressed && !wasPressed) {
        wasPressed = true;
        pressStart = millis();
    } else if (!pressed && wasPressed) {
        wasPressed = false;
        if (millis() - pressStart >= DARKFRAME_HOLD_MS) runDarkFrame();
        else triggerPrediction();
    }
}

// Optional third button: run axis (tilt/offset) calibration on release.
void handleCalButton() {
#if CAL_BUTTON_PIN >= 0
    static bool wasPressed = false;
    bool pressed = (digitalRead(CAL_BUTTON_PIN) == LOW);
    if (pressed && !wasPressed) {
        wasPressed = true;
    } else if (!pressed && wasPressed) {
        wasPressed = false;
        runAxisCalibration();
    }
#endif
}

// Serial fallback so axis calibration / dark frame / predict can be run
// from the Serial Monitor without any button.
//   c = axis calibration (locks the line), d = dark frame, p = predict,
//   a = release the axis lock and go back to auto tracking
void handleSerialCommands() {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c == 'c' || c == 'C') {
            runAxisCalibration();
        } else if (c == 'd' || c == 'D') {
            runDarkFrame();
        } else if (c == 'p' || c == 'P') {
            triggerPrediction();
        } else if (c == 'a' || c == 'A') {
            autoAlignAxis();
        }
    }
}

void handleTouchButton() {
    static unsigned long lastPoll = 0;
    static bool wasDown = false;

    if (!touchAvailable) return;

    unsigned long now = millis();
    if (now - lastPoll < TOUCH_POLL_MS) return;
    lastPoll = now;

    uint16_t x = 0, y = 0;
    bool down = readTouch(x, y);
    bool inside = down && pointInPredictButton(x, y);

    if (inside && !wasDown) {
        wasDown = true;
        drawPredictButton(true);
        gfx->flush();
        triggerPrediction();
    } else if (!down && wasDown) {
        wasDown = false;
        drawPredictButton(false);
        gfx->flush();
    }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    // Always handle web clients (non-blocking)
    server.handleClient();
    dnsServer.processNextRequest();

    // Accept and immediately close HTTPS attempts so the serial log can
    // show that the browser was trying https:// instead of http://.
    {
        WiFiClient probe = httpsProbe.available();
        if (probe) {
            Serial.printf("[443] TLS attempt from %s - use http:// not https://\n",
                          probe.remoteIP().toString().c_str());
            probe.stop();
        }
    }

    // Heartbeat: confirms the firmware is alive and whether a client is
    // actually associated with the AP (stations stays 0 if the phone/PC
    // never joined "ITIM-project").
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 5000) {
        lastHeartbeat = millis();
        Serial.printf("[hb] up=%lus stations=%d heap=%u\n",
                      millis() / 1000,
                      WiFi.softAPgetStationNum(),
                      (unsigned)ESP.getFreeHeap());
    }

    handleBootButton();
    handleCalButton();
    handleTouchButton();
    handleSerialCommands();

    static int frameCount = 0;
    static unsigned long fpsTimer = 0;
    static float fps = 0;

    unsigned long now = millis();

    // Persist a manual axis trim once the user stops clicking.
    if (axisSavePending && now - axisSaveAt > 1500) {
        saveAxisCalibration();
        axisSavePending = false;
        Serial.printf("Axis trim saved: a=%.4f b=%.1f\n", specSlope, specIntercept);
    }

    // Spectrum update at a fixed interval
    if (now - lastFrameTime < FRAME_INTERVAL_MS) {
        return;
    }
    lastFrameTime = now;

    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Frame capture failed");
        return;
    }

    // Follow the (possibly tilted) spectrum band before sampling it.
    updateSpectrumAxis(fb);
    extractSpectrum(fb, spectrumRawR, spectrumRawG, spectrumRawB);
    esp_camera_fb_return(fb);

    // Noise reduction pipeline - RGB channels stay separate throughout,
    // and nothing here is clamped to 255.
    applyDarkFrameSubtraction(spectrumRawR, spectrumRawG, spectrumRawB);
    applyTemporalAverage(spectrumRawR, spectrumRawG, spectrumRawB,
                          spectrumSmoothR, spectrumSmoothG, spectrumSmoothB);

    // Static (not stack) scratch buffers: three float[480] arrays are
    // ~5.7 KB, enough to overflow the default 8 KB loop task stack.
    static float tmpR[SPECTRUM_LEN], tmpG[SPECTRUM_LEN], tmpB[SPECTRUM_LEN];
    applyMovingAverage(spectrumSmoothR, tmpR, SPECTRUM_LEN, SMOOTH_WINDOW);
    applyMovingAverage(spectrumSmoothG, tmpG, SPECTRUM_LEN, SMOOTH_WINDOW);
    applyMovingAverage(spectrumSmoothB, tmpB, SPECTRUM_LEN, SMOOTH_WINDOW);
    memcpy(spectrumSmoothR, tmpR, sizeof(tmpR));
    memcpy(spectrumSmoothG, tmpG, sizeof(tmpG));
    memcpy(spectrumSmoothB, tmpB, sizeof(tmpB));

    // Combine to a single value ONLY for graphing / recording (real R/G/B
    // data stays intact in spectrumSmoothR/G/B for future analysis).
    combineForDisplay(spectrumSmoothR, spectrumSmoothG, spectrumSmoothB, spectrumCombined);

    // Find real (unclamped) max + its position
    float maxVal = 0;
    int peakIdx = 0;
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        if (spectrumCombined[i] > maxVal) { maxVal = spectrumCombined[i]; peakIdx = i; }
    }

    // Update web shared data
    webPeakPos = peakIdx;
    webMaxVal = maxVal;
    webFPS = fps;
    lastSpectrumMs = millis();

    // Update LCD
    drawSpectrum(spectrumCombined);
    drawColorBar();

    // Calculate FPS
    frameCount++;
    if (now - fpsTimer >= 1000) {
        fps = frameCount * 1000.0f / (now - fpsTimer);
        frameCount = 0;
        fpsTimer = now;
    }

    drawInfo(spectrumCombined, maxVal, fps);
    drawPredictButton(false);
    if (popupUntil != 0 && millis() < popupUntil) {
        drawPredictionPopup(popupValue, popupOk);
    }
    gfx->flush();
}
