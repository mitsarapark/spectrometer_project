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

// ---------------- Auto center-line (tilt) detection ----------------
// The spectrum is a bright band, but in the real camera image it is
// usually tilted and shifted. Instead of a fixed column we fit
//   x(y) = a * y + b
// to the bright band and use that line for extraction, so the yellow
// overlay on the web page and the sampled data always match the light.
float specSlope = 0.0f;             // a (pixels per row)
float specIntercept = (float)CENTER_COL; // b (column at row 0)
bool  axisDetected = false;
// Set when the axis is calibrated manually (BOOT/3rd button, serial 'c' or
// /calibrate). While locked the per-frame tracker leaves the line alone,
// so a manual calibration is not slowly pulled back to the auto fit.
// Cleared by a later calibration or by serial 'a' / /axis/auto.
bool  axisLocked = false;

#ifndef AXIS_AUTO_DETECT
#define AXIS_AUTO_DETECT 1
#endif
#define AXIS_ROW_STEP      4      // tracking step in rows
#define AXIS_MIN_ROWS      40     // need this many valid rows to trust a fit
#define AXIS_MAX_SLOPE     0.30f  // reject absurd tilts (px per row)
#define AXIS_EMA_ALPHA     0.25f  // smoothing between frames
#define AXIS_MIN_BRIGHT    12     // ignore rows darker than this (0-255)
#define AXIS_TRACK_WIN     30     // px searched around the previous row
#define AXIS_REFINE_WIN    8      // px centroid window around the row peak
#define AXIS_PEAK_FRACTION 0.5f   // row peak must be >= this * frame peak

static inline uint8_t pixelLuma(uint16_t p) {
    uint8_t r = (p >> 11) & 0x1F;
    uint8_t g = (p >> 5) & 0x3F;
    uint8_t b = p & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (uint8_t)(0.2126f * r + 0.7152f * g + 0.0722f * b);
}

// Return the sub-pixel center of the bright band inside
// [xHint - AXIS_TRACK_WIN, xHint + AXIS_TRACK_WIN], or -1 when the row
// is too dark. Only a narrow window around the local peak contributes to
// the centroid, so the dim refracted side orders cannot pull it away.
static float bandCenterAt(const uint16_t *pix, int y, int xHint, int minPeak) {
    const uint16_t *row = pix + (size_t)y * FRAME_WIDTH;
    int x0 = xHint - AXIS_TRACK_WIN;
    int x1 = xHint + AXIS_TRACK_WIN;
    if (x0 < 0) x0 = 0;
    if (x1 >= FRAME_WIDTH) x1 = FRAME_WIDTH - 1;

    uint8_t peak = 0;
    int peakX = x0;
    for (int x = x0; x <= x1; x++) {
        uint8_t v = pixelLuma(row[x]);
        if (v > peak) { peak = v; peakX = x; }
    }
    if ((int)peak < minPeak) return -1.0f;

    int cx0 = peakX - AXIS_REFINE_WIN;
    int cx1 = peakX + AXIS_REFINE_WIN;
    if (cx0 < 0) cx0 = 0;
    if (cx1 >= FRAME_WIDTH) cx1 = FRAME_WIDTH - 1;

    const float thr = peak * 0.5f;
    float wsum = 0.0f, xsum = 0.0f;
    for (int x = cx0; x <= cx1; x++) {
        uint8_t v = pixelLuma(row[x]);
        if ((float)v >= thr) {
            float w = (float)v - thr + 1.0f;
            wsum += w;
            xsum += w * (float)x;
        }
    }
    return (wsum > 0.0f) ? (xsum / wsum) : (float)peakX;
}

static void sortSmallFloats(float *v, int n) {
    for (int i = 1; i < n; i++) {
        float key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
}

// Fit x = a*y + b to the spectrum band of one frame.
//
// The dim rainbow orders on both sides of the main band are refracted
// light, not the measured spectrum, so a whole-row centroid gets pulled
// sideways. Instead we:
//   1. seed on the brightest pixel of the frame,
//   2. follow the band up and down with a small tracking window,
//   3. take a narrow centroid around each row peak,
//   4. fit with least squares + MAD-based outlier rejection.
// Returns false when there is not enough light / too few valid rows.
bool fitSpectrumAxis(camera_fb_t *fb, float &outA, float &outB) {
    const uint16_t *pix = (const uint16_t *)fb->buf;
    const int maxPts = FRAME_HEIGHT / AXIS_ROW_STEP + 2;
    static int16_t ptsY[maxPts];
    static int16_t ptsX[maxPts];
    static uint8_t keep[maxPts];
    static float res[maxPts];
    static float tmp[maxPts];
    int n = 0;

    // 1) brightest pixel -> seed row/column
    uint8_t globalPeak = 0;
    int gx = FRAME_WIDTH / 2;
    int gy = FRAME_HEIGHT / 2;
    for (int y = 0; y < FRAME_HEIGHT; y += 4) {
        const uint16_t *row = pix + (size_t)y * FRAME_WIDTH;
        for (int x = 0; x < FRAME_WIDTH; x += 4) {
            uint8_t v = pixelLuma(row[x]);
            if (v > globalPeak) { globalPeak = v; gx = x; gy = y; }
        }
    }
    if (globalPeak < AXIS_MIN_BRIGHT) return false;

    int minPeak = (int)(globalPeak * AXIS_PEAK_FRACTION);
    if (minPeak < AXIS_MIN_BRIGHT) minPeak = AXIS_MIN_BRIGHT;

    // 2) follow the band from the seed row
    float xc = bandCenterAt(pix, gy, gx, minPeak);
    if (xc < 0.0f) return false;
    ptsY[n] = (int16_t)gy;
    ptsX[n] = (int16_t)(xc + 0.5f);
    n++;

    float xprev = xc;
    for (int y = gy + AXIS_ROW_STEP; y < FRAME_HEIGHT; y += AXIS_ROW_STEP) {
        float c = bandCenterAt(pix, y, (int)(xprev + 0.5f), minPeak);
        if (c < 0.0f || fabsf(c - xprev) > (float)AXIS_TRACK_WIN) break;
        ptsY[n] = (int16_t)y;
        ptsX[n] = (int16_t)(c + 0.5f);
        n++;
        xprev = c;
    }
    xprev = xc;
    for (int y = gy - AXIS_ROW_STEP; y >= 0; y -= AXIS_ROW_STEP) {
        float c = bandCenterAt(pix, y, (int)(xprev + 0.5f), minPeak);
        if (c < 0.0f || fabsf(c - xprev) > (float)AXIS_TRACK_WIN) break;
        ptsY[n] = (int16_t)y;
        ptsX[n] = (int16_t)(c + 0.5f);
        n++;
        xprev = c;
    }

    if (n < AXIS_MIN_ROWS) return false;

    // 3) least squares with MAD-based outlier rejection
    for (int i = 0; i < n; i++) keep[i] = 1;

    float a = 0.0f, b = 0.0f;
    for (int iter = 0; iter < 3; iter++) {
        float sumY = 0, sumX = 0, sumYY = 0, sumXY = 0;
        int used = 0;
        for (int i = 0; i < n; i++) {
            if (!keep[i]) continue;
            sumY += ptsY[i];
            sumX += ptsX[i];
            sumYY += (float)ptsY[i] * ptsY[i];
            sumXY += (float)ptsY[i] * ptsX[i];
            used++;
        }
        if (used < AXIS_MIN_ROWS) return false;

        float denom = used * sumYY - sumY * sumY;
        if (fabsf(denom) < 1e-6f) return false;
        a = (used * sumXY - sumY * sumX) / denom;
        b = (sumX - a * sumY) / used;

        if (iter == 2) break;

        int m = 0;
        for (int i = 0; i < n; i++) {
            res[i] = fabsf(ptsX[i] - (a * ptsY[i] + b));
            if (keep[i]) tmp[m++] = res[i];
        }
        sortSmallFloats(tmp, m);
        float med = tmp[m / 2];
        float thr = 3.0f * med + 1.5f;
        for (int i = 0; i < n; i++) {
            if (keep[i] && res[i] > thr) keep[i] = 0;
        }
    }

    if (!isfinite(a) || !isfinite(b)) return false;
    if (fabsf(a) > AXIS_MAX_SLOPE) return false;
    // sanity: the fitted line must pass near the brightest seed pixel
    if (fabsf(a * gy + b - gx) > 20.0f) return false;

    outA = a;
    outB = b;
    return true;
}

// Keep the fitted line inside the sensor so every sampled row still has
// a full +/- COL_AVG_RANGE window.
static inline void clampSpectrumAxis() {
    const float lo = (float)COL_AVG_RANGE + 0.5f;
    const float hi = (float)(FRAME_WIDTH - 1 - COL_AVG_RANGE) - 0.5f;
    const float yMax = (float)(FRAME_HEIGHT - 1);

    float bLo = lo - fminf(0.0f, specSlope * yMax);
    float bHi = hi - fmaxf(0.0f, specSlope * yMax);
    if (bLo > bHi) {
        specSlope = 0.0f;
        bLo = lo;
        bHi = hi;
    }
    if (specIntercept < bLo) specIntercept = bLo;
    if (specIntercept > bHi) specIntercept = bHi;
}

// Per-frame update with exponential smoothing. Keeps the previous line
// when the frame has no usable signal. Does nothing while the axis is
// locked by a manual calibration.
bool updateSpectrumAxis(camera_fb_t *fb) {
    if (axisLocked) {
        (void)fb;
        return true;
    }
#if AXIS_AUTO_DETECT
    float a = 0.0f, b = 0.0f;
    if (!fitSpectrumAxis(fb, a, b)) return false;

    if (!axisDetected) {
        specSlope = a;
        specIntercept = b;
        axisDetected = true;
    } else {
        specSlope += AXIS_EMA_ALPHA * (a - specSlope);
        specIntercept += AXIS_EMA_ALPHA * (b - specIntercept);
    }
    clampSpectrumAxis();
    return true;
#else
    (void)fb;
    return false;
#endif
}

// ---------------- Extraction ----------------
// Reads along the fitted center line x(y) = a*y + b (averaged over
// +/- COL_AVG_RANGE columns) down every row -> one R/G/B value per row
// (vertical spectrum).
// The spectrum is indexed by row: index 0 = top of the sensor image.
// Values are NOT clamped or combined here - raw per-channel averages.
// Set SPECTRUM_INVERT to 1 if the spectrum appears reversed (e.g. red at
// the top instead of the bottom) so the graph reads low->high wavelength.
void extractSpectrum(camera_fb_t *fb, float *outR, float *outG, float *outB) {
    for (int row = 0; row < FRAME_HEIGHT; row++) {
        int center = (int)(specSlope * (float)row + specIntercept + 0.5f);
        int colStart = center - COL_AVG_RANGE;
        int colEnd = center + COL_AVG_RANGE;
        if (colStart < 0) colStart = 0;
        if (colEnd >= FRAME_WIDTH) colEnd = FRAME_WIDTH - 1;
        int numCols = colEnd - colStart + 1;

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
