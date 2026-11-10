#ifndef INFERENCE_H
#define INFERENCE_H

/*
 * On-device PLS regression.
 *
 * The coefficients in model_weights.h are already the combined
 * standardisation + PLS vector, so scoring is a single dot product over
 * a resampled copy of spectrumCombined. This keeps the ESP32 side tiny
 * (no TensorFlow Lite Micro, no dynamic allocation).
 */

float lastPrediction = 0.0f;
bool  hasPrediction = false;
unsigned long lastPredictionMs = 0;
bool  modelReady = (MODEL_READY != 0);

// Resample the live spectrum onto the model's input grid and score it.
// Returns false when no model has been flashed or the result is not a
// finite number.
bool predictConcentration(float &result) {
    if (!modelReady) return false;

    float acc = MODEL_B0;
    for (int i = 0; i < MODEL_INPUT_DIM; i++) {
        int idx = (MODEL_INPUT_DIM > 1)
                    ? i * (SPECTRUM_LEN - 1) / (MODEL_INPUT_DIM - 1)
                    : 0;
        if (idx < 0) idx = 0;
        if (idx >= SPECTRUM_LEN) idx = SPECTRUM_LEN - 1;
        acc += MODEL_B[i] * spectrumCombined[idx];
    }

    if (!isfinite(acc)) return false;
    result = acc;
    return true;
}

// Run a prediction and remember the result for the LCD / web UI.
bool runPrediction(float &result) {
    float v = 0.0f;
    if (!predictConcentration(v)) return false;
    lastPrediction = v;
    hasPrediction = true;
    lastPredictionMs = millis();
    result = v;
    return true;
}

#endif // INFERENCE_H
