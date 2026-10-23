#ifndef SPECTRUM_PROCESSING_H
#define SPECTRUM_PROCESSING_H

/* ==============================================================
 *  Requires the following to be #defined BEFORE including this file
 *  (set in spectrometer.ino):
 *    FRAME_WIDTH, FRAME_HEIGHT, CENTER_ROW, ROW_AVG_RANGE,
 *    SPECTRUM_LEN, TEMPORAL_FRAMES, SMOOTH_WINDOW
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
// Reads the CENTER ROW (averaged over +/- ROW_AVG_RANGE rows) across
// every column -> one R/G/B value per column (horizontal spectrum).
// Values are NOT clamped or combined here - raw per-channel averages.
void extractSpectrum(camera_fb_t *fb, float *outR, float *outG, float *outB) {
    int rowStart = CENTER_ROW - ROW_AVG_RANGE;
    int rowEnd = CENTER_ROW + ROW_AVG_RANGE;
    if (rowStart < 0) rowStart = 0;
    if (rowEnd >= FRAME_HEIGHT) rowEnd = FRAME_HEIGHT - 1;
    int numRows = rowEnd - rowStart + 1;

    for (int col = 0; col < FRAME_WIDTH; col++) {
        uint32_t rSum = 0, gSum = 0, bSum = 0;
        for (int row = rowStart; row <= rowEnd; row++) {
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
        outR[col] = rSum / (float)numRows;
        outG[col] = gSum / (float)numRows;
        outB[col] = bSum / (float)numRows;
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
    float tmpR[SPECTRUM_LEN], tmpG[SPECTRUM_LEN], tmpB[SPECTRUM_LEN];
    double sumR[SPECTRUM_LEN] = {0}, sumG[SPECTRUM_LEN] = {0}, sumB[SPECTRUM_LEN] = {0};

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
// This is a simple (R+G+B)/3 average - not an absolute optical power
// measurement (camera channels have unequal spectral sensitivity), but
// good enough as a visualization/combined-intensity metric per the
// review notes. Real per-channel data stays intact in
// spectrumSmoothR/G/B for any future analysis.
void combineForDisplay(float *r, float *g, float *b, float *out) {
    for (int i = 0; i < SPECTRUM_LEN; i++) {
        out[i] = (r[i] + g[i] + b[i]) / 3.0f;
    }
}

// ---------------- Position -> RGB (cosmetic gradient only, NOT calibrated) ----------------
// Maps a 0..1 position fraction to an approximate visible-light color,
// purely so the graph is easier to read. Assumes an approximate
// 400-700nm range spread evenly across the x-axis - this is NOT a
// wavelength calibration (that is a later project phase).
void positionToRGB(float frac, uint8_t &r, uint8_t &g, uint8_t &b) {
    float wl = 400.0f + frac * 300.0f;
    float rr = 0, gg = 0, bb = 0;

    if (wl >= 400 && wl < 440) { rr = -(wl - 440) / 40.0f; bb = 1.0; }
    else if (wl < 490) { gg = (wl - 440) / 50.0f; bb = 1.0; }
    else if (wl < 510) { gg = 1.0; bb = -(wl - 510) / 20.0f; }
    else if (wl < 580) { rr = (wl - 510) / 70.0f; gg = 1.0; }
    else if (wl < 645) { rr = 1.0; gg = -(wl - 645) / 65.0f; }
    else { rr = 1.0; }

    r = (uint8_t)(rr * 255);
    g = (uint8_t)(gg * 255);
    b = (uint8_t)(bb * 255);
}

#endif // SPECTRUM_PROCESSING_H
