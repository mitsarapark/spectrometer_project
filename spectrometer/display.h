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
 *  Also uses darkFrameCaptured + positionToRGB() from
 *  spectrum_processing.h (include that file first).
 * ============================================================== */

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

// x axis = pixel position (0..SPECTRUM_LEN-1), NOT calibrated to nm.
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

    // X axis: pixel position
    for (int i = 0; i <= 8; i++) {
        int pos = i * (SPECTRUM_LEN - 1) / 8;
        int x = GRAPH_LEFT + (i * GRAPH_WIDTH) / 8;
        gfx->drawLine(x, GRAPH_BOTTOM, x, GRAPH_BOTTOM + 5, WHITE);
        gfx->drawLine(x, GRAPH_TOP, x, GRAPH_BOTTOM, DARKGREY);
        gfx->setCursor(x - 10, GRAPH_BOTTOM + 8);
        gfx->printf("%d", pos);
    }
    gfx->setCursor(GRAPH_LEFT + GRAPH_WIDTH / 2 - 40, GRAPH_BOTTOM + 20);
    gfx->print("Position (px, not calibrated)");
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
    gfx->printf("Peak: px %d", peakIdx);

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, TEXT_Y + 25);
    gfx->printf("Max Int: %.1f (scale 0-%d)  FPS: %.1f", maxVal, (int)DISPLAY_MAX_INTENSITY, fps);

    gfx->setCursor(10, TEXT_Y + 40);
    gfx->setTextColor(YELLOW);
    gfx->printf("Cols:%d  TempAvg:%d/%d  Smooth:%d",
                COL_AVG_RANGE * 2 + 1, temporalCount, TEMPORAL_FRAMES, SMOOTH_WINDOW);

    gfx->setCursor(10, TEXT_Y + 55);
    if (darkFrameCaptured) {
        gfx->setTextColor(GREEN);
        gfx->print("Dark frame: ON");
    } else {
        gfx->setTextColor(RED);
        gfx->print("Dark frame: OFF (press BOOT)");
    }
}

#endif // DISPLAY_H
