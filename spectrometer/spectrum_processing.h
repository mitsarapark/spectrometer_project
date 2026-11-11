#ifndef SPECTRUM_PROCESSING_H
#define SPECTRUM_PROCESSING_H

/* ==============================================================
 *  Requires the following to be #defined BEFORE including this file
 *  (set in spectrometer.ino):
 *    FRAME_WIDTH, FRAME_HEIGHT, CENTER_COL, COL_AVG_RANGE,
 *    SPECTRUM_LEN, SPECTRUM_INVERT, TEMPORAL_FRAMES, SMOOTH_WINDOW,
 *    COMBINE_MODE (COMBINE_REC709 / COMBINE_REC601 / COMBINE_AVERAGE),
 *    CAL_PIXEL_1, CAL_NM_1, CAL_PIXEL_2, CAL_NM_2,
 *    SPEC_NM_MIN, SPEC_NM_MAX
 *
 *  Design notes (per review):
 *   - RGB channels are kept SEPARATE all the way through the pipeline
 *     (dark subtraction -> temporal average -> moving average). Luma
 *     (0.299R+0.587G+0.114B) is NOT used as the measurement value since
 *     it's a perceptual weighting, not an optical-power measurement.
 *     Combining into a single "display" value happens only at the very
 *     end, only for graphing.
 *   - Raw measurement data is NEVER clamped to 255. Only the
 *     visualization step (display.h) maps values into a 0..DISPLAY_MAX
 *     pixel range, so peaks above 255 are not lost from the underlying
 *     data.
 * ============================================================== */

// ---------------- Buffers ----------------
bool darkFrameCaptured = false;
float darkFrameR[SPECTRUM_LEN];
float darkFrameG[SPECTRUM_LEN];
float darkFrameB[SPECTRUM_LEN];

float spectrumRawR[SPECTRUM_LEN];
float spectrumRawG[SPECTRUM_LEN];
float spectrumRawB[SPECTRUM_LEN];

float spectrumSmoothR[SPECTRUM_LEN];
float spectrumSmoothG[SPECTRUM_LEN];
float spectrumSmoothB[SPECTRUM_LEN];

// Combined value, computed ONLY for display/graphing (see combineForDisplay())
float spectrumCombined[SPECTRUM_LEN];

float spectrumTemporalR[TEMPORAL_FRAMES][SPECTRUM_LEN];
float spectrumTemporalG[TEMPORAL_FRAMES][SPECTRUM_LEN];
float spectrumTemporalB[TEMPORAL_FRAMES][SPECTRUM_LEN];
uint8_t temporalIndex = 0;
uint8_t temporalCount = 0; // how many real frames have been fed in so far (<= TEMPORAL_FRAMES)

// ---------------- Extraction ----------------
// Reads the CENTER COLUMN (averaged over +/- COL_AVG_RANGE columns)
// down every row -> one R/G/B value per row (vertical spectrum).
// The spectrum is indexed by row: index 0 = top of the sensor image.
// Values are NOT clamped or combined here - raw per-channel averages.
// Set SPECTRUM_INVERT to 1 if the spectrum appears reversed (e.g. red at
// the top instead of the bottom) so the graph reads low->high wavelength.
void extractSpectrum(camera_fb_t *fb, float *outR, float *outG, float *outB) {
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

        int outIndex = SPECTRUM_INVERT ? (FRAME_HEIGHT - 1 - row) : row;
        outR[outIndex] = rSum / (float)numCols;
        outG[outIndex] = gSum / (float)numCols;
        outB[outIndex] = bSum / (float)numCols;
    }
}

// ---------------- Temporal buffer reset ----------------
// Call after capturing a new dark frame (or before starting a new
// measurement session) so old frames don't get mixed with new ones.
void resetTemporalBuffer() {
    temporalIndex = 0;
    temporalCount = 0;
    for (int f = 0; f < TEMPORAL_FRAMES; f++) {
        for (int i = 0; i < SPECTRUM_LEN; i++) {
            spectrumTemporalR[f][i] = 0;
            spectrumTemporalG[f][i] = 0;
            spectrumTemporalB[f][i] = 0;
        }
    }
}

// ---------------- Dark frame ----------------
void captureDarkFrame() {
    // These buffers are static on purpose: float[480] x3 + double[480] x3
    // would need ~17 KB of stack and overflow the Arduino loop task
    // (8 KB), which shows up as "Stack canary watchpoint triggered".
    static float tmpR[SPECTRUM_LEN], tmpG[SPECTRUM_LEN], tmpB[SPECTRUM_LEN];
    static float sumR[SPECTRUM_LEN], sumG[SPECTRUM_LEN], sumB[SPECTRUM_LEN];

    for (int i = 0; i < SPECTRUM_LEN; i++) {
        sumR[i] = 0; sumG[i] = 0; sumB[i] = 0;
    }

    for (int f = 0; f < 8; f++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;
        extractSpectrum(fb, tmpR, tmpG, tmpB);
        esp_camera_fb_return(fb);
        for (int i = 0; i < SPECTRUM_LEN; i++) {
            sumR[i] += tmpR[i];
            sumG[i] += tmpG[i];
            sumB[i] += tmpB[i];
        }
        delay(100);
    }
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        darkFrameR[i] = sumR[i] / 8.0;
        darkFrameG[i] = sumG[i] / 8.0;
        darkFrameB[i] = sumB[i] / 8.0;
    }

    darkFrameCaptured = true;
    resetTemporalBuffer(); // don't mix pre-darkframe frames with post-darkframe frames
}

// Subtracts the dark baseline. Only clamps at a LOWER bound of 0 (light
// intensity can't be negative) - never clamps an upper bound, so real
// peaks are preserved.
void applyDarkFrameSubtraction(float *r, float *g, float *b) {
    if (!darkFrameCaptured) return;
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        r[i] -= darkFrameR[i]; if (r[i] < 0) r[i] = 0;
        g[i] -= darkFrameG[i]; if (g[i] < 0) g[i] = 0;
        b[i] -= darkFrameB[i]; if (b[i] < 0) b[i] = 0;
    }
}

// ---------------- Temporal averaging (fixed: no /4 during warm-up) ----------------
void applyTemporalAverage(float *curR, float *curG, float *curB,
                           float *outR, float *outG, float *outB) {
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        spectrumTemporalR[temporalIndex][i] = curR[i];
        spectrumTemporalG[temporalIndex][i] = curG[i];
        spectrumTemporalB[temporalIndex][i] = curB[i];
    }
    temporalIndex = (temporalIndex + 1) % TEMPORAL_FRAMES;
    if (temporalCount < TEMPORAL_FRAMES) temporalCount++;

    for (int i = 0; i < SPECTRUM_LEN; i++) {
        float sumR = 0, sumG = 0, sumB = 0;
        for (int f = 0; f < temporalCount; f++) {
            sumR += spectrumTemporalR[f][i];
            sumG += spectrumTemporalG[f][i];
            sumB += spectrumTemporalB[f][i];
        }
        outR[i] = sumR / temporalCount;
        outG[i] = sumG / temporalCount;
        outB[i] = sumB / temporalCount;
    }
}

// ---------------- Moving average (spatial smoothing) ----------------
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

// ---------------- Combine RGB -> single value FOR DISPLAY ONLY ----------------
// Weights follow a standard luma definition instead of a flat average:
//   Rec.709 : 0.2126 R + 0.7152 G + 0.0722 B  (sRGB / HDTV)
//   Rec.601 : 0.299  R + 0.587  G + 0.114  B  (SDTV / JPEG)
//   Average : (R + G + B) / 3
// Every option keeps the weights summing to 1.0, so the 0..255-ish
// intensity range (and DISPLAY_MAX_INTENSITY) stays comparable.
// Note: the sensor output is gamma-encoded, so true Rec.709 luma would
// require linearising first; this is the common approximation and it is
// applied consistently to both the LCD and the CSV dataset.
// Real per-channel data stays intact in spectrumSmoothR/G/B.
void combineForDisplay(float *r, float *g, float *b, float *out) {
#if COMBINE_MODE == COMBINE_REC601
    const float wr = 0.299f, wg = 0.587f, wb = 0.114f;
#elif COMBINE_MODE == COMBINE_AVERAGE
    const float wr = 1.0f / 3.0f, wg = 1.0f / 3.0f, wb = 1.0f / 3.0f;
#else
    const float wr = 0.2126f, wg = 0.7152f, wb = 0.0722f;
#endif
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        out[i] = wr * r[i] + wg * g[i] + wb * b[i];
    }
}

// ---------------- Pixel index -> wavelength (2-point linear calibration) ----------------
// CAL_PIXEL_1/2 are row indices and CAL_NM_1/2 the wavelengths of two
// known lines. The default configuration maps the full sensor column to
// 380-700 nm; replace CAL_* with measured reference lines (e.g. a
// fluorescent tube's 405 nm and 546 nm peaks) for a real calibration.
// Results are clamped to the SPEC_NM_MIN..SPEC_NM_MAX measured window.
inline float pixelToNm(float idx) {
    const int span = (int)CAL_PIXEL_2 - (int)CAL_PIXEL_1;
    float nm;
    if (span == 0) {
        nm = (float)CAL_NM_1;
    } else {
        const float slope = ((float)CAL_NM_2 - (float)CAL_NM_1) / (float)span;
        nm = (float)CAL_NM_1 + slope * (idx - (float)CAL_PIXEL_1);
    }
    if (nm < SPEC_NM_MIN) nm = SPEC_NM_MIN;
    if (nm > SPEC_NM_MAX) nm = SPEC_NM_MAX;
    return nm;
}

// ---------------- Wavelength -> approximate visible color ----------------
// Cosmetic only: used for the graph fill and the color bar so the plot is
// easier to read. Values outside 380..780 nm are clamped.
void wavelengthToRGB(float wl, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (wl < 380.0f) wl = 380.0f;
    if (wl > 780.0f) wl = 780.0f;

    float rr = 0, gg = 0, bb = 0;

    if (wl < 440) { rr = -(wl - 440) / (440 - 380); bb = 1.0; }
    else if (wl < 490) { gg = (wl - 440) / (490 - 440); bb = 1.0; }
    else if (wl < 510) { gg = 1.0; bb = -(wl - 510) / (510 - 490); }
    else if (wl < 580) { rr = (wl - 510) / (580 - 510); gg = 1.0; }
    else if (wl < 645) { rr = 1.0; gg = -(wl - 645) / (645 - 580); }
    else { rr = 1.0; }

    float factor = 1.0f;
    if (wl < 420) factor = 0.3f + 0.7f * (wl - 380) / (420 - 380);
    else if (wl > 700) factor = 0.3f + 0.7f * (780 - wl) / (780 - 700);

    r = (uint8_t)(rr * factor * 255);
    g = (uint8_t)(gg * factor * 255);
    b = (uint8_t)(bb * factor * 255);
}

// Convenience wrapper: 0..1 position along the spectrum -> color.
void positionToRGB(float frac, uint8_t &r, uint8_t &g, uint8_t &b) {
    wavelengthToRGB(pixelToNm(frac * (SPECTRUM_LEN - 1)), r, g, b);
}

#endif // SPECTRUM_PROCESSING_H
