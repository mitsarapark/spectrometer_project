# ML pipeline (PLSR)

Workflow for turning spectrometer measurements into an on-device
concentration prediction.

```
ESP32 (record samples)  ->  dataset.csv  ->  train_plsr.py  ->  model_weights.h  ->  reflash
```

## 1. Collect data on the device

1. Put the reference sample in the spectrometer and wait ~2 s for the
   graph to settle (temporal averaging needs a few frames).
2. Open `http://192.168.4.1`, enter the known concentration and press
   **Record sample**. One CSV row is appended to LittleFS.
3. Repeat for every sample. Use several concentrations, ideally
   including repeats, so the model can separate signal from noise.
4. Press **Download CSV** to save `dataset.csv` to your PC.

CSV layout (written by `spectrometer/dataset.h`):

```
405.0,406.2,...,700.0,concentration
  12.3, 11.9,...,  4.2, 0.250
  ...
```

* header = wavelength of each column (from the 2-point calibration)
* data rows = combined intensity (Rec.709 luma of R/G/B)
* last column = known concentration

## 2. Train on the PC

```bash
cd ml
python -m pip install -r requirements.txt
python train_plsr.py --csv dataset.csv --unit "%"
```

What the script does:

1. loads the CSV and validates it,
2. optionally augments the data (Gaussian noise, baseline offset,
   gain drift - `--no-augment` to disable),
3. standardises the spectra,
4. searches `n_components` with 5-fold cross-validation,
5. fits the final PLS model,
6. collapses standardisation + PLS into a single coefficient vector and
   writes `../spectrometer/model_weights.h`.

Options worth knowing:

| flag | meaning |
|---|---|
| `--unit "%"` | unit string shown on the LCD / web page |
| `--max-components 15` | upper bound for the CV search |
| `--augment-copies 5` | synthetic variants per real sample |
| `--noise 0.01` | noise sigma relative to each row's max |
| `--baseline 1.0` | max absolute baseline offset (intensity units) |
| `--scale 0.03` | +/-3 % multiplicative gain drift |

## 3. Flash and predict

Copy/replace `spectrometer/model_weights.h`, rebuild, upload.

* `MODEL_READY 1` makes the PREDICT button active,
* tap **PREDICT** on the LCD (or short-press BOOT) to run a prediction,
* the result also appears on the web page.

## Important consistency rules

The model is only valid under the **same measurement conditions** it was
trained on. If any of these change, collect a new dataset and retrain:

* `MANUAL_EXPOSURE` / `MANUAL_GAIN`
* `COL_AVG_RANGE`, `TEMPORAL_FRAMES`, `SMOOTH_WINDOW`
* `COMBINE_MODE` (Rec.709 / Rec.601 / average)
* the 2-point calibration (`CAL_*`) - this changes the wavelength axis
* `CSV_POINTS` - must equal `MODEL_INPUT_DIM`

## Dataset size guidance

| samples | expectation |
|---|---|
| < 10 | only a sanity check, model will not generalise |
| 20-50 | usable for a narrow concentration range |
| 100+ | reasonable for real measurements |

PLS is a linear method: it works well when the spectral response is
roughly proportional to concentration. If the relationship is strongly
non-linear, consider a small MLP later (the firmware interface is a
single `predictConcentration()` call, so it can be swapped).
