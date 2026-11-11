#ifndef DATASET_H
#define DATASET_H

/*
 * Dataset capture for the ML pipeline.
 *
 * Each recorded sample appends one CSV row to the device filesystem:
 *   nm_0,nm_1,...,nm_{K-1},concentration
 * The header holds the wavelength of every column (from the 2-point
 * calibration), data rows hold the combined intensity at that
 * wavelength, and the last column is the known concentration.
 *
 * The storage backend is chosen at boot:
 *   - LittleFS when the partition table has a SPIFFS partition
 *     ("Huge APP (3MB No OTA/1MB SPIFFS)")
 *   - FFat when it has a FAT partition instead
 *     ("16M Flash (3MB APP/9.9MB FATFS)")
 * so the dataset works with either partition scheme.
 *
 * The file is served by /dataset.csv for training on a PC.
 */

#include <FS.h>
#include <LittleFS.h>
#include <FFat.h>
#include <esp_partition.h>

#define DATASET_PATH   "/dataset.csv"
#define CSV_POINTS     120          // must match MODEL_INPUT_DIM in the header
#define CSV_MAX_BYTES  (900 * 1024) // keep headroom on the 1 MB / 9.9 MB partitions

#ifdef MODEL_INPUT_DIM
#if CSV_POINTS != MODEL_INPUT_DIM
#error "CSV_POINTS must match MODEL_INPUT_DIM in model_weights.h"
#endif
#endif

int datasetSampleCount = 0;
bool datasetReady = false;
fs::FS *datasetFS = nullptr;

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
    if (datasetFS == nullptr) return 0;
    fs::File f = datasetFS->open(DATASET_PATH, FILE_READ);
    if (!f) return 0;
    int lines = 0;
    while (f.available()) {
        if (f.read() == '\n') lines++;
    }
    f.close();
    return (lines > 0) ? (lines - 1) : 0; // minus the header row
}

bool initDataset() {
    // 1) SPIFFS/LittleFS partition (Huge APP scheme)
    const esp_partition_t *spiffs = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

    if (spiffs != NULL) {
        Serial.printf("LittleFS partition '%s' at 0x%06x, %u KB\n",
                      spiffs->label, (unsigned)spiffs->address,
                      (unsigned)(spiffs->size / 1024));
        if (LittleFS.begin(false) || LittleFS.begin(true)) {
            datasetFS = &LittleFS;
        }
    }

    // 2) FAT partition (16M Flash 3MB APP/9.9MB FATFS scheme)
    if (datasetFS == nullptr) {
        const esp_partition_t *fat = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
        if (fat != NULL) {
            Serial.printf("FAT partition '%s' at 0x%06x, %u KB\n",
                          fat->label, (unsigned)fat->address,
                          (unsigned)(fat->size / 1024));
            if (FFat.begin(false, "/ffat", 10, fat->label) ||
                FFat.begin(true, "/ffat", 10, fat->label)) {
                datasetFS = &FFat;
            }
        }
    }

    if (datasetFS == nullptr) {
        Serial.println("No usable dataset partition (need SPIFFS or FATFS).");
        Serial.println("Pick Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)");
        Serial.println("or 16M Flash (3MB APP/9.9MB FATFS).");
        datasetReady = false;
        return false;
    }

    datasetReady = true;
    datasetSampleCount = countDatasetSamples();
    Serial.printf("Dataset ready: %d samples in %s\n", datasetSampleCount, DATASET_PATH);
    return true;
}

bool appendSample(float conc) {
    if (!datasetReady || datasetFS == nullptr) return false;

    fs::File f = datasetFS->open(DATASET_PATH, FILE_APPEND);
    if (!f) return false;

    // Refuse to fill the partition: leave headroom for the firmware and
    // filesystem metadata.
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
    if (!datasetReady || datasetFS == nullptr) return;
    datasetFS->remove(DATASET_PATH);
    datasetSampleCount = 0;
    Serial.println("Dataset cleared");
}

#endif // DATASET_H
