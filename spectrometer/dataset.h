#ifndef DATASET_H
#define DATASET_H

/*
 * Dataset capture for the ML pipeline.
 *
 * Each recorded sample appends one CSV row to LittleFS:
 *   nm_0,nm_1,...,nm_{K-1},concentration
 * The header holds the wavelength of every column (from the 2-point
 * calibration), data rows hold the combined intensity at that
 * wavelength, and the last column is the known concentration.
 *
 * The file is served by /dataset.csv for training on a PC.
 */

#include <LittleFS.h>
#include <esp_partition.h>

#define DATASET_PATH   "/dataset.csv"
#define CSV_POINTS     120          // must match MODEL_INPUT_DIM in the header
#define CSV_MAX_BYTES  (900 * 1024) // keep well inside the 1 MB SPIFFS partition

#ifdef MODEL_INPUT_DIM
#if CSV_POINTS != MODEL_INPUT_DIM
#error "CSV_POINTS must match MODEL_INPUT_DIM in model_weights.h"
#endif
#endif

int datasetSampleCount = 0;
bool datasetReady = false;

// Same resampling grid as inference.h: sample i maps to
// i * (SPECTRUM_LEN - 1) / (CSV_POINTS - 1), so the training rows and
// the inference vector always represent the same wavelengths.
static int csvIndexFor(int i) {
    int idx = (CSV_POINTS > 1)
                ? i * (SPECTRUM_LEN - 1) / (CSV_POINTS - 1)
                : 0;
    if (idx < 0) idx = 0;
    if (idx >= SPECTRUM_LEN) idx = SPECTRUM_LEN - 1;
    return idx;
}

static int countDatasetSamples() {
    File f = LittleFS.open(DATASET_PATH, FILE_READ);
    if (!f) return 0;
    int lines = 0;
    while (f.available()) {
        if (f.read() == '\n') lines++;
    }
    f.close();
    return (lines > 0) ? (lines - 1) : 0; // minus the header row
}

bool initDataset() {
    // A missing SPIFFS/LittleFS partition is the usual reason for
    // "Failed to initialize LittleFS" - it means the upload used a
    // partition scheme without one (select "Huge APP (3MB No OTA/1MB
    // SPIFFS)" in the Arduino IDE).
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

    if (part == NULL) {
        Serial.println("No SPIFFS/LittleFS partition in this build.");
        Serial.println("Select Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)");
        datasetReady = false;
        return false;
    }
    Serial.printf("Dataset partition '%s' at 0x%06x, %u KB\n",
                  part->label, (unsigned)part->address,
                  (unsigned)(part->size / 1024));

    datasetReady = LittleFS.begin(false);
    if (!datasetReady) {
        Serial.println("LittleFS mount failed, formatting partition...");
        datasetReady = LittleFS.begin(true);
    }
    if (!datasetReady) {
        Serial.println("LittleFS mount failed - dataset disabled");
        return false;
    }

    datasetSampleCount = countDatasetSamples();
    Serial.printf("Dataset ready: %d samples in %s\n", datasetSampleCount, DATASET_PATH);
    return true;
}

bool appendSample(float conc) {
    if (!datasetReady) return false;

    File f = LittleFS.open(DATASET_PATH, FILE_APPEND);
    if (!f) return false;

    // Refuse to fill the partition: leave headroom for the firmware and
    // LittleFS metadata.
    if (f.size() >= CSV_MAX_BYTES) {
        f.close();
        Serial.println("Dataset full - clear it before recording more samples");
        return false;
    }

    if (f.size() == 0) {
        for (int i = 0; i < CSV_POINTS; i++) {
            f.print(pixelToNm(csvIndexFor(i)), 1);
            f.print(',');
        }
        f.println("concentration");
    }

    for (int i = 0; i < CSV_POINTS; i++) {
        f.print(spectrumCombined[csvIndexFor(i)], 1);
        f.print(',');
    }
    f.println(conc, 3);
    f.close();

    datasetSampleCount++;
    return true;
}

void clearDataset() {
    if (!datasetReady) return;
    LittleFS.remove(DATASET_PATH);
    datasetSampleCount = 0;
    Serial.println("Dataset cleared");
}

#endif // DATASET_H
