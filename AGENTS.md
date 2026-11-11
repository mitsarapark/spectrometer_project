# AGENTS.md

คู่มือสำหรับ AI agent / ผู้พัฒนาที่กลับมาทำงานต่อในโปรเจกต์นี้
**อ่านไฟล์นี้ก่อนแก้โค้ดทุกครั้ง** — เนื้อหาด้านล่างคือสถานะจริงของโค้ด ณ commit ล่าสุด

อัปเดตล่าสุด: 2026-11-11

---

## 1. เป้าหมายโปรเจกต์

เครื่อง spectrometer แบบพกพาบนบอร์ด **Waveshare ESP32-S3-Touch-LCD-3.5B**
ใช้กล้อง onboard อ่านสเปกตรัมที่ผ่าน grating (กระจายแนวตั้ง) แล้ว:

1. dark frame capture
2. อ่านสเปกตรัมจาก **แนวกลางของแถบแสง** (auto tilt/offset detection)
3. วาดกราฟ intensity vs wavelength (nm) บน LCD
4. WiFi AP + เว็บดูภาพกล้อง/กราฟ พร้อมเส้น/แถบแสดงแนวที่วัด
5. เก็บ dataset CSV ลง flash ผ่านเว็บ → เทรน PLSR บน PC
6. รันโมเดลบน ESP32 ทำนายความเข้มข้นเมื่อกดปุ่ม
7. captive portal + mDNS ให้เข้าเว็บง่าย

---

## 2. สถานะปัจจุบัน (สรุปสั้น)

**ทำงานได้แล้ว**
- กล้อง RGB565 VGA + LCD + กราฟ + color bar + info
- WiFi AP `ITIM-project` / `http://192.168.4.1` (port 80, ไม่ต้องใส่ port)
- captive portal (เด้งหน้าเว็บเองหลังต่อ WiFi) + mDNS `http://spectrometer.local`
- dark frame (BOOT ค้าง), temporal/moving average, Rec.709 combine
- 2-point wavelength calibration (ค่า default 380–700 nm)
- auto center-line: fit `x(y)=a*y+b` ตามแถบหลัก, **lock + NVS** (ไม่มี auto-track), เส้นเหลืองฝังในภาพ `/capture` จากเฟรมเดียวกัน
- axis calibration (`/calibrate`, ปุ่มที่สามถ้าต่อ, serial `c`)
- dataset CSV ลง LittleFS **หรือ** FFat (เลือกตาม partition อัตโนมัติ) + ดาวน์โหลด/ล้าง
- PLSR training pipeline (`ml/`) + on-device dot-product inference
- ปุ่ม PREDICT บนจอ (ถ้า touch ใช้ได้) + BOOT สำรอง

**ยังค้าง / ต้องทำต่อ**
- **Touch**: เดิมไม่เจอ (`0x3B: not found`) เพราะ Bus เดิม 21/22 ผิด — แก้เป็น **8/7 + Waveshare BSP** (`bsp_touch_init`) แล้ว รอทดสอบบนเครื่อง
- **ปุ่มกายภาพ**: ผู้ใช้บอก BOOT ใช้ไม่ได้ และมีปุ่มอื่นทำ dark/predict อยู่ — ยังไม่ทราบ GPIO
  (`DARKFRAME_BUTTON_PIN = 0`, `CAL_BUTTON_PIN = -1` ต้องแก้เมื่อรู้ GPIO)
- **Wavelength calibration จริง** (405/546 nm จากหลอด fluorescent) ยังไม่ได้วัด
- **โมเดลจริง** ยังเป็น placeholder (`MODEL_READY 0`)
- Fe analysis / multi-peak analysis

---

## 3. โครงสร้างไฟล์

```
spectrometer/                 <-- sketch หลัก (ชื่อโฟลเดอร์ต้องตรงกับ .ino)
  spectrometer.ino            setup/loop, camera, WiFi/WebServer/HTML, ปุ่ม, pipeline
  spectrum_processing.h       axis detection, extraction, dark frame, averaging,
                              combine, calibration (pixelToNm), wavelengthToRGB
  display.h                   วาดกราฟ/แกน/color bar/ข้อมูล/ปุ่ม PREDICT/ป็อปอัปผล
  dataset.h                   CSV ลง LittleFS หรือ FFat + นับ sample + clear
  inference.h                 PLS dot product บนเครื่อง
  model_weights.h             โมเดลจาก ml/ (placeholder => MODEL_READY 0)
  touch.h                     อ่าน touch AXS15231B @ 0x3B ผ่าน BSP (bus 8/7, fallback ไป BOOT)
ml/                           เทรน PLSR บน PC
  train_plsr.py               CSV -> augment -> CV เลือก n_components -> export header
  augment.py                  noise / baseline / gain drift
  README.md                   วิธีเก็บข้อมูล + เทรน + flash
test/                         โค้ดทดสอบ display/camera (reference เท่านั้น)
PROJECT_STATUS.md             สถานะ + ค่าคอนฟิก + roadmap
problem.md                    บันทึกปัญหาที่เจอ/วิธีแก้ (P1-P8)
README.md                     ภาพรวม + build/flash
```

โฟลเดอร์ `final/` ถูกลบแล้ว (ประวัติอยู่ใน git)

---

## 4. Hardware / Board settings

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** (จำเป็น)
- Partition: **Huge APP (3MB No OTA/1MB SPIFFS)** หรือ **16M Flash (3MB APP/9.9MB FATFS)**
  (โค้ดเลือก LittleFS/FFat เอง; ผู้ใช้ใช้ FATFS อยู่)
- USB CDC On Boot: **Enabled**
- Libraries: `esp32-camera` (มากับ core), `Arduino_GFX`, `TCA9554`

### Pin map (ห้ามเดา)

```
LCD QSPI : CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
Camera   : XCLK=38, SIOD=8, SIOC=7, D7..D0=21,39,40,42,46,48,47,45,
           VSYNC=17, HREF=18, PCLK=41, PWDN=-1, RESET=-1
I2C      : SDA=8, SCL=7 (TCA9554 @0x20, touch @0x3B — แชร์กับ camera SCCB)
BOOT     : GPIO0 (INPUT_PULLUP, active LOW)
```

> หมายเหตุ: touch/TCA9554 อยู่บน **8/7** ไม่ใช่ 21/22 (GPIO21 เป็น camera D7)
> `initTouch()` ต้อง `Wire.end()/Wire.begin(8,7)` ใหม่หลัง `esp_camera_init()`
> เพราะกล้องยึด pad เดียวกันไปแล้ว

---

## 5. Data flow

```
esp_camera_fb_get() [RGB565 VGA 640x480]
  -> updateSpectrumAxis(fb)   no-op เมื่อ locked / AXIS_AUTO_DETECT=0 (แนวไม่ขยับเอง)
  -> extractSpectrum(fb)      อ่านตามแนว x(y) ± COL_AVG_RANGE -> R/G/B ต่อ row
  -> applyDarkFrameSubtraction()
  -> applyTemporalAverage()   4 เฟรม
  -> applyMovingAverage()     window 7
  -> combineForDisplay()      Rec.709 luma
  -> LCD: drawSpectrum/drawColorBar/drawInfo/drawPredictButton -> gfx->flush()
  -> Web: /data (JSON 80 จุด + nm + axis + model) + /capture (JPEG)
  -> /record?conc=.. -> dataset.h -> /dataset.csv
  -> predictConcentration(): dot product บน spectrumCombined
```

กฎสำคัญ:
- **ห้าม clamp ข้อมูลจริง** ที่ 255; clamp เฉพาะตอนแปลงเป็นพิกัด pixel
- RGB แยก channel ตลอด; combine เฉพาะกราฟ/CSV/โมเดล
- `CSV_POINTS` ต้องเท่ากับ `MODEL_INPUT_DIM` (มี `#error` กันไว้)
- การ resample ต้องใช้ grid เดียวกันทั้ง `dataset.h` และ `inference.h`
  (`i * (SPECTRUM_LEN - 1) / (N - 1)`)

---

## 6. ค่าคอนฟิกหลัก (`spectrometer/spectrometer.ino`)

| Macro | ค่า | ความหมาย |
|---|---|---|
| `CENTER_COL` | 320 | fallback เมื่อยัง detect แนวไม่ได้ |
| `COL_AVG_RANGE` | 10 | เฉลี่ย ±10 คอลัมน์รอบแนว |
| `SPECTRUM_LEN` | 480 | = FRAME_HEIGHT |
| `SPECTRUM_INVERT` | 0 | 1 = กลับลำดับ row |
| `TEMPORAL_FRAMES` | 4 | จำนวนเฟรมเฉลี่ย |
| `SMOOTH_WINDOW` | 7 | moving average |
| `FRAME_INTERVAL_MS` | 300 | คาบอัปเดต |
| `COMBINE_MODE` | `COMBINE_REC709` | Rec.709 / Rec.601 / average |
| `CAL_PIXEL_1/2` | 0 / 479 | default เต็มคอลัมน์ |
| `CAL_NM_1/2` | 380 / 700 | ช่วง visible (default) |
| `SPEC_NM_MIN/MAX` | 380 / 700 | clamp ผล `pixelToNm()` |
| `MANUAL_EXPOSURE` | 300 | tune กับแสงจริง |
| `MANUAL_GAIN` | 0 | tune กับแสงจริง |
| `DISPLAY_MAX_INTENSITY` | 300 | สเกลจอ (ไม่ clamp ข้อมูล) |
| `DARKFRAME_BUTTON_PIN` | 0 | ปุ่ม action (สั้น=predict, ค้าง=dark) |
| `DARKFRAME_HOLD_MS` | 1500 | เวลากดค้าง |
| `CAL_BUTTON_PIN` | -1 | ปุ่มที่สาม (axis calibrate) — ยังไม่ต่อ |
| `TOUCH_ENABLE` | 1 | เปิด/ปิด touch |
| `TOUCH_I2C_ADDR` | 0x3B | AXS15231B touch |
| `TOUCH_POLL_MS` | 60 | คาบ poll touch |

Axis detection (`spectrum_processing.h`):

| Macro | ค่า | ความหมาย |
|---|---|---|
| `AXIS_AUTO_DETECT` | **0** | 0 = เส้นขยับเฉพาะตอนสั่ง calibrate (ตั้งใน `spectrometer.ino`) |
| `AXIS_ROW_STEP` | 4 | sample ทุก 4 แถว |
| `AXIS_MIN_ROWS` | 40 | จำนวนจุดขั้นต่ำที่เชื่อถือ |
| `AXIS_MAX_SLOPE` | 0.30 | |a| สูงสุด (px/row) |
| `AXIS_EMA_ALPHA` | 0.25 | ความลื่นระหว่างเฟรม (โหมด auto เท่านั้น) |
| `AXIS_MIN_BRIGHT` | 12 | กัน noise ในเฟรมมืด |
| `AXIS_PRED_WIN` | 24 | หน้าต่างค้นหา ±px รอบตำแหน่งที่ทำนาย |
| `AXIS_SELECT_FRACTION` | 0.80 | เลือก peak ที่ใกล้ prediction ถ้าความสว่าง ≥ 80% ของ peak ในหน้าต่าง |
| `AXIS_PEAK_FRACTION` | 0.35 | row peak ต้อง ≥ 35% ของ frame peak |
| `AXIS_INLIER_PX` | 3.0 | เกณฑ์ outlier ของ robust fit |

**Axis calibration**: `/calibrate`, ปุ่มที่สาม หรือ serial `c` → fit 20 เฟรม → median `a`,`b` → **lock + บันทึก NVS**
(`Preferences` namespace `axis`) ค่าจึงคงอยู่ข้าม reboot และไม่มี auto-tracking มาเขียนทับ
`/axis/auto` หรือ serial `a` = fit เฟรมเดียวแล้ว lock (one-shot, ไม่รันเอง)

---

## 7. Web UI / Endpoints

| Path | งาน |
|---|---|
| `/` | หน้าเว็บ (กราฟ + ภาพกล้อง + overlay + Dataset panel) |
| `/data` | JSON: `peak`, `peakNm`, `max`, `scale`, `nmStart/End`, `axisA/B/On`, `col`, `colRange`, `modelReady`, `hasPred`, `pred`, `modelUnit`, `samples`, `fps`, `spectrum[80]` |
| `/capture` | JPEG snapshot |
| `/record?conc=<value>` | บันทึกแถว CSV (ปฏิเสธถ้าสเปกตรัมเก่า > 3 s) |
| `/dataset.csv` | ดาวน์โหลด CSV |
| `/dataset/clear` | ล้าง dataset |
| `/calibrate` | เก็บ 20 เฟรม → median `a`,`b` แล้ว **lock** แนว (คืน `ok`,`locked`) |
| `/axis` | `{"a":..,"b":..,"detected":..,"locked":..}` |
| `/axis/auto` | fit เฟรมเดียวแล้ว lock + บันทึก NVS (one-shot) |
| `/ping` | `pong` (ทดสอบเร็ว) |

อื่น ๆ: captive portal redirect ทุก path ที่ไม่รู้จัก, `[443]` probe log เมื่อ browser ใช้ HTTPS

---

## 8. ปุ่ม / input

| อินพุต | ผล |
|---|---|
| BOOT สั้น (<1.5 s) | predict |
| BOOT ค้าง (>=1.5 s) | dark frame |
| `CAL_BUTTON_PIN` (ถ้าตั้ง) | axis calibration (lock แนว) |
| Touch ปุ่ม PREDICT บนจอ | predict (ถ้า touch เจอ) |
| Serial `c` | axis calibration (lock แนว) |
| Serial `a` | auto align ครั้งเดียว (fit + lock + NVS) |
| Serial `d` | dark frame |
| Serial `p` | predict |

**ต้องแก้เมื่อรู้ GPIO**: `DARKFRAME_BUTTON_PIN`, `CAL_BUTTON_PIN`

---

## 9. ML workflow

1. วัด sample ที่รู้ความเข้มข้น → เว็บกรอกค่า → **Record sample**
2. ทำหลายความเข้มข้น (แนะนำ ≥20 samples)
3. **Download CSV**
4. ```bash
   cd ml
   python -m pip install -r requirements.txt
   python train_plsr.py --csv dataset.csv --unit "<หน่วย>"
   ```
5. คัดลอก `ml/model_weights.h` (ค่า default path คือ `spectrometer/model_weights.h`) ทับ แล้ว flash ใหม่
6. `MODEL_READY` จะเป็น 1 → PREDICT ใช้งานได้

ข้อบังคับ: เปลี่ยน exposure/gain/calibration/COMBINE_MODE/CSV_POINTS → **เก็บ dataset ใหม่และเทรนใหม่**

---

## 10. การ build / flash

เครื่อง develop ปกติ**ไม่มี** `arduino-cli` หรือ `pio` (ตรวจก่อนใช้)

1. เปิด `spectrometer/spectrometer.ino` ใน Arduino IDE
2. Board: ESP32S3 Dev Module / OPI PSRAM / partition ตามข้อ 4 / USB CDC On Boot = Enabled
3. Verify / Upload
4. ต่อ WiFi `ITIM-project` (`itimsarapark`) → หน้าเว็บเด้งเอง หรือ `http://192.168.4.1`
5. กด BOOT ค้างเพื่อ dark frame (ปิดช่องรับแสง)

---

## 11. Debug quick reference

Log ที่ควรเห็นตอนบูต:
```
WiFi AP started: ITIM-project
IP address: 192.168.4.1
Captive portal DNS started
mDNS: http://spectrometer.local
Web server started on port 80
Open: http://192.168.4.1/   (no port needed, HTTP/80)
FAT partition 'ffat' at 0x...... , xxxx KB
Dataset ready: N samples in /dataset.csv
Touch 0x3B: found
Axis loaded from NVS: a=... b=... (locked)
Setup complete. Starting capture...
Serial commands: c=axis calibrate (locks), d=dark frame, p=predict, a=auto align
[hb] up=..s stations=.. heap=..
```

อาการ → ตรวจ:
| อาการ | ตรวจ |
|---|---|
| เปิดเว็บไม่ได้ | `[ap] client connected` มีไหม, `GET /` มีไหม, `[443]` ขึ้นไหม (browser ใช้ https) |
| บอร์ดรีบูต | `Stack canary` / panic → ดู P8 ใน `problem.md` |
| เส้นเหลืองไม่ทับแถบ | `/axis` (`detected`,`locked`), กด **Calibrate axis** / **Auto align** ใหม่, ดู `AXIS_PEAK_FRACTION` / `AXIS_PRED_WIN` |
| dataset ไม่ขึ้น | log partition backend (LittleFS/FFat) |
| ค่า intensity ต่ำ | ตรวจ `a`,`b` ว่าตรงแนว, tune exposure |

เครื่องมือทดสอบ offline (ไม่ต้องมี ESP32): compile `spectrum_processing.h` ด้วย g++ + stub
แล้วสร้างเฟรมสังเคราะห์เรียก `fitSpectrumAxis()` — ใช้ตรวจ regression ของ algorithm ได้

---

## 12. Known issues / gotchas

- **Touch**: แก้เป็น bus **8/7 + BSP** แล้ว (เดิม 21/22 ผิด) — ถ้ายังไม่ขึ้นให้ดู log `Touch 0x3B:` ตอนบูต และ fallback ไป BOOT
- **ปุ่มจริงยังไม่ผูก**: BOOT อาจเสีย, ต้องตั้ง `DARKFRAME_BUTTON_PIN`/`CAL_BUTTON_PIN` ให้ตรง
- **Stack**: ห้าม array ใหญ่ (~2 KB+) บน stack ของ loop task; ใช้ `static` (เคย crash canary)
- ค่า calibration เป็น 380–700 แบบ uncalibrated; ต้องวัดหลอด fluorescent จริง
- `/capture` แย่ง frame กับ loop (ยอมรับได้)
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ยังไม่ tune
- ยังไม่ init PMU (AXP2101) — บาง revision อาจต้องทำก่อนใช้กล้อง
- หน้าเว็บ `https://` ใช้ไม่ได้ (มีแต่ HTTP 80) — ถ้า browser บังคับ https จะเห็น `[443]` ใน log
- `test/` เป็น reference เก่า ห้ามใช้ build หลัก

---

## 13. กติกาการทำงาน

- **ทุกครั้งที่แก้ไฟล์ ให้ commit ทันที** หลังตรวจแล้ว การ commit แต่ละครั้งให้ใส่สิ่งที่ทำลงไปไม่ใช่ชื่อมั่ว
- ก่อน commit: `git status`, `git diff`, เช็กไม่มี secret
- commit message สั้น ตรงงาน prefix: `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`
- **ห้าม push ขึ้น remote เอง** เว้นแต่ผู้ใช้สั่ง
- ไม่ใส่ comment ที่ไม่จำเป็น; โค้ดมี comment เฉพาะส่วนสำคัญ
- ไม่แน่ใจ hardware/tuning → ถามผู้ใช้ก่อนเดา

---

## 14. Checklist ก่อนส่งงาน

1. `SPECTRUM_LEN` / buffer / index bounds ตรงกัน (480)
2. ไม่มี identifier เก่า (`CENTER_ROW`, `ROW_AVG_RANGE`, `AXIS_COL_STEP`)
3. แก้ HTML/JS → `handleData()` ส่ง field ครบตามที่ JS ใช้
4. แก้ `model_weights.h` → `MODEL_INPUT_DIM` == `CSV_POINTS` == จำนวนคอลัมน์ CSV
5. อัปเดต `PROJECT_STATUS.md` และ (ถ้ามีปัญหาใหม่) `problem.md`
6. Compile ผ่าน syntax check ด้วย stub (ถ้าทำได้) + JS `node --check`

---

## 15. ประวัติสำคัญ (commit map)

| Commit | สิ่งที่ทำ |
|---|---|
| `49853b1` | snapshot ก่อน refactor |
| `d31acaa` | ย้าย `files (4)/` → `spectrometer/` |
| `edbe048` | อ่านคอลัมน์กลาง + overlay แนวตั้ง + 300 ms |
| `71fb27a` | AGENTS/PROJECT_STATUS/README |
| `cc7365d` | ลบ `final/` |
| `a9de3f2` | Rec.709, 2-point calibration, dataset, inference |
| `616af03` | docs ML |
| `e95a534` | จำกัดขนาด dataset |
| `1f3b7ba` | map 380–700 nm + clamp |
| `a26213b` | diagnostics `/ping`, AP event, TLS probe |
| `b3be4d2` | FFat + captive portal + mDNS |
| `3604d8b` | เริ่ม web server ก่อน splash |
| `af1a461` | ย้าย buffer ใหญ่ off-stack (แก้ canary) |
| `b81443c` | auto axis (tilt/offset) + calibrate |
| `a526655` | serial commands c/d/p |
| `9687e08` | แก้ scope bug ของ axis fit |
| `3d13bab` | เปลี่ยนเป็น track แถบหลัก (กันแสงหักเหข้าง ๆ) |
| `68ccbcd` | lock แนวเมื่อ calibrate + ย้าย touch ไป I2C 8/7 ด้วย BSP driver |
| `177c733` | vendor Waveshare AXS15231B touch driver ใน sketch folder |
| `b6cdeeb` | calibrate แบบ manual เท่านั้น + NVS, fit half-max midpoint, เส้นเหลืองฝังใน /capture |
