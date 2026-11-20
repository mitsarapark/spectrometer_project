# AGENTS.md

คู่มือสำหรับ AI agent / ผู้พัฒนาที่กลับมาทำงานต่อในโปรเจกต์นี้
**อ่านไฟล์นี้ก่อนแก้โค้ดทุกครั้ง** — เนื้อหาด้านล่างคือสถานะจริงของโค้ด ณ commit ล่าสุด

อัปเดตล่าสุด: 2026-11-17

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
- dark frame (BOOT ค้าง), temporal/moving average, equal-weight `(R+G+B)/3` combine
- 2-point wavelength calibration (ค่า default 380–700 nm)
- auto center-line: fit `x(y)=a*y+b` ตามแถบหลัก, **lock + NVS** (ไม่มี auto-track), เส้นเหลืองฝังในภาพ `/capture` จากเฟรมเดียวกัน
- row order runtime: auto-detect ปลายน้ำเงิน (red/blue centroid) + `/spectrum/flip` + `/spectrum/orient` + serial `i`, เก็บ NVS (`inv`)
- wavelength calibration runtime: N-point (สูงสุด 8) px→nm ผ่านเว็บ (หา peak อัตโนมัติ/กรอกเอง) + NVS (`cal`), fallback เป็น 2-point `CAL_*`
- **เมนู calibrate บนจอ** (ปุ่ม MENU ข้าง PREDICT): axis calibrate / dark frame / flip+auto row order / เลือกหลอด reference (แตะซ้ำ) / calibrate wavelength จาก preset / back — ใช้ได้แม้เปิดเว็บไม่ได้; BOOT สั้น = เลื่อน, ค้าง = รัน, serial `m` เปิดเมนู
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
  display.h                   วาดกราฟ/แกน/color bar/ข้อมูล/ปุ่ม MENU+PREDICT/
                              หน้าจอเมนู calibrate/ป็อปอัปผล
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
problem.md                    บันทึกปัญหาที่ยังเปิดอยู่ + ข้อมูลที่ต้องเก็บ (P1-P5)
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
  -> detectSpectrumOrientation() ครั้งแรก/ตอน calibrate (หา blue end)
  -> extractSpectrum(fb)      อ่านตามแนว x(y) ± COL_AVG_RANGE -> R/G/B ต่อ row + flip ตาม spectrumInvert
  -> applyDarkFrameSubtraction()
  -> applyTemporalAverage()   4 เฟรม
  -> applyMovingAverage()     window 7
  -> combineForDisplay()      average (R+G+B)/3
  -> pixelToNm()              N-point piecewise map (fallback 2-point) สำหรับแกน/CSV/peak
  -> LCD: drawSpectrum/drawColorBar/drawInfo/drawPredictButton/drawFuncButton -> gfx->flush()
     (ข้ามการวาดทั้งหมดเมื่อเมนู calibrate เปิดอยู่ — pipeline ยังวัดต่อ)
  -> Web: /data (JSON 160 จุด + nm + axis + order + model) + /capture (JPEG)
  -> /record?conc=.. -> dataset.h -> /dataset.csv
  -> predictConcentration(): dot product บน spectrumCombined
```

กฎสำคัญ:
- **ห้าม clamp ข้อมูลจริง** ที่ 255; clamp เฉพาะตอนแปลงเป็นพิกัด pixel
- RGB แยก channel ตลอด; combine เฉพาะกราฟ/CSV/โมเดล
- combine ค่าเริ่มต้นคือ equal-weight `(R+G+B)/3` — **ห้ามใช้ Rec.709 เป็นค่า spectral power** (blue เหลือ 7.2%)
- index 0 ของสเปกตรัมต้องเป็นปลายน้ำเงินเสมอ; ถ้าเปลี่ยน combine/gain/order ต้องเก็บ dataset ใหม่
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
| `SPECTRUM_INVERT` | 0 | ค่าเริ่มต้นตอนบูต (runtime auto/flip ได้) |
| `SPECTRUM_AUTO_ORIENT` | 1 | 1 = auto-detect ปลายน้ำเงินจากเฟรมแรก |
| `TEMPORAL_FRAMES` | 4 | จำนวนเฟรมเฉลี่ย |
| `SMOOTH_WINDOW` | 7 | moving average |
| `FRAME_INTERVAL_MS` | 300 | คาบอัปเดต |
| `COMBINE_MODE` | `COMBINE_AVERAGE` | equal-weight `(R+G+B)/3`; Rec.709/601 เป็น legacy |
| `COMBINE_GAIN_R/G/B` | 1.0 (ปิด) | trim ราย channel หลังวัด white reference |
| `WEB_SPECTRUM_POINTS` | 160 | จำนวนจุดกราฟเว็บ (bin average) |
| `CAL_PIXEL_1/2` | 0 / 479 | fallback 2-point (เต็มคอลัมน์) |
| `CAL_NM_1/2` | 380 / 700 | fallback 2-point (visible) |
| `CAL_MAX_POINTS` | 8 | จำนวนจุดสูงสุดของ runtime calibration (NVS `cal`) |
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
| `LAMP_TOL_ROWS` | 4.0 | tolerance (row) ตอนจับคู่เส้นหลอด reference ในเมนู |
| `LAMP_MERGE_NM` | 3.5 | เส้นที่ห่าง ≤ ค่านี้ใช้ peak เดียวกันได้ (577/579) |
| `LAMP_MIN_SPAN_NM` | 50 | span nm ต่ำสุดของจุดที่จับคู่ได้ก่อนยอมรับ calibration |

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
| `AXIS_PEAK_FRACTION` | 0.15 | row peak ต้อง ≥ 15% ของ frame peak |
| `AXIS_INLIER_PX` | 3.0 | เกณฑ์ outlier ของ robust fit |

**Axis calibration**: `/calibrate`, ปุ่มที่สาม หรือ serial `c` → fit 20 เฟรม → median `a`,`b` → **lock + บันทึก NVS**
(`Preferences` namespace `axis`) ค่าจึงคงอยู่ข้าม reboot และไม่มี auto-tracking มาเขียนทับ
`/axis/auto` หรือ serial `a` = fit เฟรมเดียวแล้ว lock (one-shot, ไม่รันเอง)
ถ้ายังไม่ตรงแนว ใช้แถบ **Axis trim** บนหน้าเว็บ (`/axis/nudge`, `/axis/set`, `/axis/reset`, `/axis/save`) ขยับ tilt/offset เองได้

**Row order**: `detectSpectrumOrientation()` เทียบ centroid ของแสงแดงกับน้ำเงิน (ลบ dark offset)
ถ้าแดงอยู่ row น้อยกว่าน้ำเงินจะ flip; ต้องมีทั้งสองปลายจึงตัดสินได้ (monochromatic → คงค่าเดิม)
ผลถูกเก็บ NVS key `inv` พร้อม axis; `/spectrum/flip` และ serial `i` ใช้สลับเองได้

**Wavelength calibration**: ถ้ามีจุด calibrate ≥2 จุดใน NVS (`cal`) `pixelToNm()` จะใช้ piecewise-linear
ผ่านจุดที่วัด (ไม่ใช่ 2-point เดิม) ทำให้ dispersion ที่ไม่เป็นเชิงเส้นตรงขึ้น; ถ้าไม่มีจะ fallback ไป `CAL_*`
- `/cal/peaks` หา peak จาก `spectrumCombined` (sub-pixel, prominence, แยกกันอย่างน้อย ~5 row)
- `/cal/set?points=<px>:<nm>,...` บันทึก 2-8 จุด (px/nma ต้องเพิ่มขึ้น และอยู่ใน `SPEC_NM_MIN..SPEC_NM_MAX`)
- `/cal/reset` ล้างกลับไปใช้ 2-point fallback
- `/spectrum/flip` / serial `i` จะ **mirror ตาราง cal** ตามลำดับ row และบันทึกทั้ง axis+cal พร้อมกัน
- ลำดับ row ต้องเป็นน้ำเงิน→แดงก่อน calibrate ไม่งั้นจุดจะเรียง nm จากมากไปน้อยและถูก reject

**On-screen calibration menu** (`MENU` button ใน `display.h`, โลจิกใน `spectrometer.ino`):
ใช้เมื่อเปิดเว็บไม่ได้ — เมนูมี 7 แถว: Axis calibrate / Dark frame / Flip row order / Auto row order /
Lamp preset / Calibrate wavelength / Back. `menuOpen=true` แล้ว loop จะ **ไม่วาดกราฟ** (ยังวัดต่อ)
- `Lamp preset` แตะซ้ำเพื่อวน preset ที่บรรจุในโค้ด: Fluorescent (405/436/546.1/577/579.1),
  Hydrogen (410.2/434/486.1/656.3), Neon (585.2/588.2/594.5/614.3/640.2/650.7/667.8),
  Cadmium (467.8/480/508.6/643.8)
- `Calibrate wavelength` = `matchLampLines()` จับคู่เส้นหลอดกับ peak ที่ตรวจได้ โดยลองทุก
  (คู่เส้น × คู่ peak) เป็นสมการ affine row = scale*nm + offset แล้วเลือกสมการที่อธิบายเส้นได้มากสุด
  (RANSAC) — **ไม่ใช้ `pixelToNm()` เดิม** จึง calibrate ได้แม้ mapping ยังเพี้ยน
  - tolerance เป็น row (`LAMP_TOL_ROWS` 4) ไม่ใช่ nm → สมการที่บีบสเปกตรัมจนแคบจับ noise ไม่ได้
  - เส้นที่ห่างกัน ≤ `LAMP_MERGE_NM` (577/579) ใช้ peak เดียวกันได้ แต่บันทึกเป็น row เดียว
  - ต้องได้ ≥3 row และ span ≥ `LAMP_MIN_SPAN_NM` (50 nm) ไม่งั้นปฏิเสธและคงตารางเดิม
  - สำเร็จ → `setWavelengthCalibration()` + `saveWavelengthCalibration()` (NVS `cal` ชุดเดียวกับเว็บ)
- BOOT: สั้น = เลื่อนไฮไลต์, ค้าง = รันแถวที่ไฮไลต์ (เมนูจึงใช้ได้แม้ touch เสีย); serial `m` เปิด/ปิดเมนู

---

## 7. Web UI / Endpoints

| Path | งาน |
|---|---|
| `/` | หน้าเว็บ (กราฟ + ภาพกล้อง + overlay + Axis trim + Dataset panel) |
| `/data` | JSON: `peak`, `peakNm`, `max`, `scale`, `nmStart/End`, `nmMin/Max`, `specLen`, `calCount`, `calActive`, `calPoints`, `axisA/B/On`, `invert`, `orientKnown`, `combine`, `col`, `colRange`, `modelReady`, `hasPred`, `pred`, `modelUnit`, `samples`, `fps`, `spectrum[160]` |
| `/capture` | JPEG snapshot |
| `/record?conc=<value>` | บันทึกแถว CSV (ปฏิเสธถ้าสเปกตรัมเก่า > 3 s) |
| `/dataset.csv` | ดาวน์โหลด CSV |
| `/dataset/clear` | ล้าง dataset |
| `/calibrate` | เก็บ 20 เฟรม → median `a`,`b` แล้ว **lock** แนว (คืน `ok`,`locked`) |
| `/axis` | `{"a":..,"b":..,"detected":..,"locked":..}` |
| `/axis/auto` | fit เฟรมเดียวแล้ว lock + บันทึก NVS (one-shot) |
| `/axis/nudge?da=<a>&db=<b>` | ขยับแนวเองทีละนิด (tilt a / offset b px) → lock + บันทึก NVS (หน่วง 1.5 s) |
| `/axis/set?a=<a>&b=<b>` | ตั้งค่า `a`,`b` แบบ absolute → lock + บันทึก NVS |
| `/axis/reset` | กลับเป็นเส้นตรงกลาง (`a=0`, `b=CENTER_COL`) แล้ว lock |
| `/axis/save` | บันทึก NVS ทันที (flush ค่าที่ค้างอยู่) |
| `/spectrum/flip` | สลับลำดับ row (blue↔red) + lock + NVS |
| `/spectrum/orient` | auto-detect ปลายน้ำเงินจากเฟรมปัจจุบัน + lock + NVS |
| `/cal` | `{"count","active","nmStart","nmEnd","points":[[px,nm],...]}` |
| `/cal/peaks` | peak ที่ตรวจได้: `px`, `nm` (ตาม cal ปัจจุบัน), `val`, `prom` |
| `/cal/set?points=<px>:<nm>,...` | บันทึก 2-8 จุด (ต้องเรียง px/nm ขึ้น และอยู่ใน 380-700) |
| `/cal/reset` | ล้าง calibration กลับไปใช้ `CAL_*` |
| `/ping` | `pong` (ทดสอบเร็ว) |

อื่น ๆ: captive portal redirect ทุก path ที่ไม่รู้จัก, `[443]` probe log เมื่อ browser ใช้ HTTPS

---

## 8. ปุ่ม / input

| อินพุต | ผล |
|---|---|
| BOOT สั้น (<1.5 s) | predict (ถ้าเมนูเปิด = เลื่อนไปรายการถัดไป) |
| BOOT ค้าง (>=1.5 s) | dark frame (ถ้าเมนูเปิด = รันรายการที่ไฮไลต์) |
| `CAL_BUTTON_PIN` (ถ้าตั้ง) | axis calibration (lock แนว) |
| Touch ปุ่ม PREDICT บนจอ | predict (ถ้า touch เจอ) |
| Touch ปุ่ม MENU บนจอ | เปิดเมนู calibrate (แตะแถวเพื่อรัน) |
| Serial `c` | axis calibration (lock แนว) |
| Serial `a` | auto align ครั้งเดียว (fit + lock + NVS) |
| Serial `d` | dark frame |
| Serial `p` | predict |
| Serial `i` | flip ลำดับ row (blue↔red) + NVS |
| Serial `m` | เปิด/ปิดเมนู calibrate บนจอ (แล้วใช้ BOOT เลื่อน/รัน) |
| Serial `w` | พิมพ์ calibration points + peak ที่ตรวจได้ |

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

ข้อบังคับ: เปลี่ยน exposure/gain/calibration/COMBINE_MODE/COMBINE_GAIN/CSV_POINTS → **เก็บ dataset ใหม่และเทรนใหม่**

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
Spectrum order loaded from NVS: normal (blue at top)
Spectrum combine: average, row order: normal
Wavelength calibration loaded: 3 points, 380.0-700.0 nm
Setup complete. Starting capture...
Serial commands: c=axis calibrate (locks), d=dark frame, p=predict, a=auto align, i=flip spectrum order
[hb] up=..s stations=.. heap=..
```

อาการ → ตรวจ:
| อาการ | ตรวจ |
|---|---|
| เปิดเว็บไม่ได้ | `[ap] client connected` มีไหม, `GET /` มีไหม, `[443]` ขึ้นไหม (browser ใช้ https) |
| บอร์ดรีบูต | `Stack canary` / panic → array ใหญ่บน stack (กฎใน §12) |
| เส้นเหลืองไม่ทับแถบ | `/axis` (`detected`,`locked`), กด **Calibrate axis** / **Auto align** ใหม่, ดู `AXIS_PEAK_FRACTION` / `AXIS_PRED_WIN` |
| dataset ไม่ขึ้น | log partition backend (LittleFS/FFat) |
| ค่า intensity ต่ำ | ตรวจ `a`,`b` ว่าตรงแนว, tune exposure |
| ปลายน้ำเงินต่ำผิดปกติ/อยู่ผิดข้าง | `/data` (`invert`,`orientKnown`,`combine`), กด **Auto detect order** หรือ **Flip spectrum**; หลอดไฟต้องมีทั้งปลายแดงและน้ำเงิน |
| สเปกตรัมกลับด้าน (แดงซ้าย) | `/spectrum/flip` หรือ serial `i`; ค่าเก็บใน NVS |
| peak nm ไม่ตรงกับหลอด reference | หน้า **Wavelength calibration** → Find peaks → กรอก nm จริง → Apply; ตรวจ `/data` (`calCount`,`nmStart/End`); serial `w` พิมพ์ peak ทั้งหมด |
| เปิดเว็บไม่ได้แต่ต้อง calibrate | กด **MENU** บนจอ (หรือ serial `m`) แล้วเลือกรายการ — BOOT สั้นเลื่อน, ค้างรัน; ดู `menuStatus` บนจอและ log `Lamp cal (...)` |

เครื่องมือทดสอบ offline (ไม่ต้องมี ESP32): compile `spectrum_processing.h` ด้วย g++ + stub
แล้วสร้างเฟรมสังเคราะห์เรียก `fitSpectrumAxis()` / `detectSpectrumOrientation()` — ใช้ตรวจ regression ของ algorithm ได้

---

## 12. Known issues / gotchas

- **Touch**: แก้เป็น bus **8/7 + BSP** แล้ว (เดิม 21/22 ผิด) — ถ้ายังไม่ขึ้นให้ดู log `Touch 0x3B:` ตอนบูต และ fallback ไป BOOT
- **ปุ่มจริงยังไม่ผูก**: BOOT อาจเสีย, ต้องตั้ง `DARKFRAME_BUTTON_PIN`/`CAL_BUTTON_PIN` ให้ตรง
- **Stack**: ห้าม array ใหญ่ (~2 KB+) บน stack ของ loop task; ใช้ `static` (เคย crash canary)
- ค่า fallback ของ wavelength ยังเป็น 380–700 แบบ uncalibrated; ใช้หน้า **Wavelength calibration** วัดเส้นจริง (เช่น 405/546 nm) แล้วบันทึก — ไม่ต้อง reflash
- Calibration points ต้องอยู่ใน `SPEC_NM_MIN..SPEC_NM_MAX` (380–700) และเรียง px/nm ขึ้น; ถ้าต้องใช้ UV (<380) ต้องขยาย macro ก่อน
- เรียงลำดับ row (blue→red) ให้ถูกก่อน calibrate ไม่งั้นจุดจะถูก reject เพราะ nm เรียงกลับด้าน
- Auto row order ต้องมีทั้งปลายแดง/น้ำเงิน; แหล่ง monochromatic จะคงค่าเดิม (`orientKnown=false`)
- เมนูบนจอ (preset หลอด) ต้องมีเส้นของหลอดนั้น ≥3 เส้นและ span ≥50 nm; ถ้าไม่ผ่านจะขึ้นข้อความและคงตารางเดิม
  หลอดนอก preset (เช่น Na) ต้องใช้หน้าเว็บ `/cal/set` หรือเพิ่ม preset ใน `LAMP_PRESETS`
- เปิดเมนูต้องมี touch หรือ serial `m`; ถ้า BOOT เสียและ touch ไม่เจอ จะสั่ง calibrate ได้ทางเว็บ/serial เท่านั้น
- ช่อง B ของกล้องอาจตอบสนองต่ำกว่า G จริง; ถ้าปลายน้ำเงินยังอ่อนเทียบ white reference ให้วัดแล้วตั้ง `COMBINE_GAIN_B` (แล้วเทรนใหม่)
- saturation ถูกตั้งเป็น 0 เพื่อไม่ให้ ISP ผสม R/G/B; อย่าลดติดลบอีก
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
| `0ab187e` | equal-weight combine, runtime row order (auto/flip/NVS), max-channel axis brightness, saturation 0 |
| `1bd1f8c` | runtime N-point wavelength calibration (peak finder, web panel, NVS `cal`) |
| `cd02134` | เมนู calibrate บนจอ (ปุ่ม MENU + preset หลอด reference + matcher แบบ affine RANSAC, BOOT/serial `m` navigate) |
