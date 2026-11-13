#ifndef DISPLAY_H
#define DISPLAY_H

/* ==============================================================
 *  Requires the following to be defined/declared BEFORE including
 *  this file (done in spectrometer.ino):
 *    - gfx (Arduino_Canvas*), TCA (TCA9554), GFX_BL
 *    - RGB565 color defines (BLACK, WHITE, DARKGREY, ... as raw hex)
 *    - RGB565(r,g,b) macro
 *    - GRAPH_LEFT/RIGHT/TOP/BOTTOM/WIDTH/HEIGHT, COLORBAR_Y/HEIGHT,
 *      TEXT_Y, DISP_WIDTH, DISP_HEIGHT
 *    - SPECTRUM_LEN, COL_AVG_RANGE, TEMPORAL_FRAMES, SMOOTH_WINDOW
 *    - DISPLAY_MAX_INTENSITY (display scale - NOT a data clamp; see
 *      spectrum_processing.h, raw data is never clamped)
 *    - MODEL_UNIT from model_weights.h (include that file first)
 *  Also uses darkFrameCaptured + pixelToNm()/positionToRGB() from
 *  spectrum_processing.h, and the prediction globals from inference.h
 *  (declared below as extern, defined later in the same translation unit).
 * ============================================================== */

extern bool hasPrediction;
extern float lastPrediction;
extern bool modelReady;

// On-screen PREDICT button (touch). Drawn on top of the info area every
// frame, so it must be re-drawn after drawInfo() clears that region.
#define BTN_X        (DISP_WIDTH - 150)
#define BTN_Y        434
#define BTN_W        140
#define BTN_H        34

bool initDisplay() {
    Wire.begin(I2C_SDA_GPIO, I2C_SCL_GPIO);
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

// x axis = wavelength (nm) from the 2-point calibration.
// y axis = intensity, scaled 0..DISPLAY_MAX_INTENSITY (this is a
// display-only scale - the underlying data is never clamped, see
// spectrum_processing.h).
void drawAxes() {
    gfx->drawRect(GRAPH_LEFT, GRAPH_TOP, GRAPH_WIDTH, GRAPH_HEIGHT, WHITE);

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);

    // Y axis: intensity, 0..DISPLAY_MAX_INTENSITY in 5 steps
    for (int step = 0; step <= 5; step++) {
        float val = (DISPLAY_MAX_INTENSITY * step) / 5.0f;
        int y = GRAPH_BOTTOM - (int)((val / DISPLAY_MAX_INTENSITY) * GRAPH_HEIGHT);
        gfx->drawLine(GRAPH_LEFT - 5, y, GRAPH_LEFT, y, WHITE);
        gfx->drawLine(GRAPH_LEFT, y, GRAPH_RIGHT, y, DARKGREY);
        gfx->setCursor(2, y - 4);
        gfx->printf("%d", (int)val);
    }

    // X axis: wavelength in nm
    for (int i = 0; i <= 8; i++) {
        int pos = i * (SPECTRUM_LEN - 1) / 8;
        int x = GRAPH_LEFT + (i * GRAPH_WIDTH) / 8;
        gfx->drawLine(x, GRAPH_BOTTOM, x, GRAPH_BOTTOM + 5, WHITE);
        gfx->drawLine(x, GRAPH_TOP, x, GRAPH_BOTTOM, DARKGREY);
        gfx->setCursor(x - 10, GRAPH_BOTTOM + 8);
        gfx->printf("%d", (int)pixelToNm(pos));
    }
    gfx->setCursor(GRAPH_LEFT + GRAPH_WIDTH / 2 - 25, GRAPH_BOTTOM + 20);
    gfx->print("Wavelength (nm)");
}

// spectrum[] holds REAL (unclamped) combined intensity values. Only the
// on-screen y coordinate is scaled/clamped against DISPLAY_MAX_INTENSITY
// - the array itself is untouched.
void drawSpectrum(float *spectrum) {
    gfx->fillRect(GRAPH_LEFT + 1, GRAPH_TOP + 1, GRAPH_WIDTH - 1, GRAPH_HEIGHT - 1, BLACK);

    // Fill area under curve, colored per position (cosmetic gradient)
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        float frac = (float)i / (SPECTRUM_LEN - 1);
        int x = GRAPH_LEFT + (int)(frac * GRAPH_WIDTH);
        if (x > GRAPH_RIGHT) x = GRAPH_RIGHT;

        float normalized = spectrum[i] / DISPLAY_MAX_INTENSITY;
        if (normalized > 1.0f) normalized = 1.0f; // display-only clamp
        if (normalized < 0.0f) normalized = 0.0f;

        int y = GRAPH_BOTTOM - (int)(normalized * GRAPH_HEIGHT);

        uint8_t r, g, b;
        positionToRGB(frac, r, g, b);
        uint16_t color = RGB565(r, g, b);

        if (y < GRAPH_BOTTOM) {
            gfx->drawLine(x, GRAPH_BOTTOM, x, y, color);
        }
    }

    // White outline on top of the fill
    int prevX = -1, prevY = -1;
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        float frac = (float)i / (SPECTRUM_LEN - 1);
        int x = GRAPH_LEFT + (int)(frac * GRAPH_WIDTH);
        if (x > GRAPH_RIGHT) x = GRAPH_RIGHT;

        float normalized = spectrum[i] / DISPLAY_MAX_INTENSITY;
        if (normalized > 1.0f) normalized = 1.0f;
        if (normalized < 0.0f) normalized = 0.0f;

        int y = GRAPH_BOTTOM - (int)(normalized * GRAPH_HEIGHT);

        if (prevX >= 0) gfx->drawLine(prevX, prevY, x, y, WHITE);
        prevX = x;
        prevY = y;
    }

    // Small warning if any real value exceeds the current display scale
    // (data itself is intact - this just tells you the scale is too low
    // to show the full peak right now).
    float trueMax = 0;
    for (int i = 0; i < SPECTRUM_LEN; i++) if (spectrum[i] > trueMax) trueMax = spectrum[i];
    if (trueMax > DISPLAY_MAX_INTENSITY) {
        gfx->setTextSize(1);
        gfx->setTextColor(RED);
        gfx->setCursor(GRAPH_LEFT, GRAPH_TOP - 12);
        gfx->print("! peak above display scale, raw data intact");
    }
}

void drawColorBar() {
    int barW = GRAPH_WIDTH;
    for (int x = 0; x < barW; x++) {
        float frac = (float)x / barW;
        uint8_t r, g, b;
        positionToRGB(frac, r, g, b);
        uint16_t color = RGB565(r, g, b);
        gfx->fillRect(GRAPH_LEFT + x, COLORBAR_Y, 1, COLORBAR_HEIGHT, color);
    }
    gfx->drawRect(GRAPH_LEFT, COLORBAR_Y, barW, COLORBAR_HEIGHT, WHITE);
}

// spectrum[] = real unclamped combined values; maxVal = true max (may be
// well above DISPLAY_MAX_INTENSITY - that's fine, shown as text here).
void drawInfo(float *spectrum, float maxVal, float fps) {
    int peakIdx = 0;
    float peakVal = 0;
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        if (spectrum[i] > peakVal) { peakVal = spectrum[i]; peakIdx = i; }
    }

    gfx->fillRect(0, TEXT_Y, DISP_WIDTH, DISP_HEIGHT - TEXT_Y, BLACK);

    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, TEXT_Y);
    gfx->printf("Peak: %.0f nm", pixelToNm(peakIdx));

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, TEXT_Y + 25);
    gfx->printf("Max Int: %.1f (scale 0-%d)  FPS: %.1f", maxVal, (int)DISPLAY_MAX_INTENSITY, fps);

    gfx->setCursor(10, TEXT_Y + 40);
    gfx->setTextColor(YELLOW);
    gfx->printf("Cols:%d Tmp:%d/%d Sm:%d a:%+.3f%s%s",
                COL_AVG_RANGE * 2 + 1, temporalCount, TEMPORAL_FRAMES,
                SMOOTH_WINDOW, specSlope,
                axisLocked ? " LOCK" : "",
                spectrumInvert ? " INV" : "");

    gfx->setCursor(10, TEXT_Y + 55);
    if (darkFrameCaptured) {
        gfx->setTextColor(GREEN);
        gfx->print("Dark frame: ON");
    } else {
        gfx->setTextColor(RED);
        gfx->print("Dark frame: OFF (hold BOOT)");
    }

    gfx->setCursor(10, TEXT_Y + 70);
    if (!modelReady) {
        gfx->setTextColor(DARKGREY);
        gfx->print("Model: not loaded");
    } else if (hasPrediction) {
        gfx->setTextColor(CYAN);
        gfx->printf("Pred: %.2f %s", lastPrediction, MODEL_UNIT);
    } else {
        gfx->setTextColor(WHITE);
        gfx->print("Model ready - press PREDICT");
    }
}

// On-screen PREDICT button. Re-drawn after every drawInfo() because
// drawInfo() clears the whole info area.
void drawPredictButton(bool pressed) {
    gfx->fillRect(BTN_X, BTN_Y, BTN_W, BTN_H, pressed ? BLUE : DARKBLUE);
    gfx->drawRect(BTN_X, BTN_Y, BTN_W, BTN_H, CYAN);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(BTN_X + 18, BTN_Y + 9);
    gfx->print("PREDICT");
    gfx->setTextSize(1);
}

bool pointInPredictButton(uint16_t x, uint16_t y) {
    return (x >= BTN_X) && (x < BTN_X + BTN_W) &&
           (y >= BTN_Y) && (y < BTN_Y + BTN_H);
}

// Transient result box shown for a few seconds after a prediction.
void drawPredictionPopup(float value, bool ok) {
    const int w = 250, h = 90;
    const int x = (DISP_WIDTH - w) / 2;
    const int y = 160;

    gfx->fillRect(x, y, w, h, BLACK);
    gfx->drawRect(x, y, w, h, ok ? GREEN : RED);
    gfx->drawRect(x + 1, y + 1, w - 2, h - 2, ok ? GREEN : RED);

    gfx->setTextSize(1);
    gfx->setTextColor(ok ? GREEN : RED);
    gfx->setCursor(x + 12, y + 10);
    gfx->print(ok ? "Predicted concentration" : "Model not ready");

    gfx->setTextSize(3);
    gfx->setTextColor(WHITE);
    gfx->setCursor(x + 12, y + 34);
    if (ok) gfx->printf("%.2f %s", value, MODEL_UNIT);
    else gfx->print("--");

    gfx->setTextSize(1);
}

#endif // DISPLAY_H
