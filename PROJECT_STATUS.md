# PROJECT STATUS — ESP32-S3 Spectrometer

อัปเดตล่าสุด: 2026-11-17

## 1. ภาพรวม

เครื่อง spectrometer พกพาบนบอร์ด **Waveshare ESP32-S3-Touch-LCD-3.5B**
ใช้กล้อง onboard จับภาพสเปกตรัมที่กระจายตัว **แนวตั้ง** ผ่าน grating
อ่านค่าจาก **คอลัมน์กลาง** ของภาพไปวาดกราฟบนจอ LCD
เปิด WiFi AP ให้เข้าดูภาพกล้องและกราฟผ่านเว็บ
เก็บ dataset ลง flash เพื่อเทรน PLSR บน PC แล้วรันโมเดลบนเครื่องเพื่อทำนายความเข้มข้น

## 2. สถาปัตยกรรม / Data flow

```
Camera (RGB565 VGA 640x480)
   │
   ├─ updateSpectrumAxis()           track แถบหลัก + fit x(y)=a*y+b (EMA + MAD rejection)
   ├─ detectSpectrumOrientation()    หาว่าปลายน้ำเงินอยู่ด้านไหนของ sensor (auto ครั้งแรก / ตอน calibrate)
   ├─ extractSpectrum()              อ่านตามแนว x(y) ± COL_AVG_RANGE → R/G/B ต่อ row + flip ตาม spectrumInvert
   ├─ applyDarkFrameSubtraction()    ลบ baseline (ถ้าจับ dark frame แล้ว)
   ├─ applyTemporalAverage()         เฉลี่ย 4 เฟรมล่าสุด
   ├─ applyMovingAverage()           smooth window 7
   ├─ combineForDisplay()            ค่าเฉลี่ยเท่ากันทุกช่วงคลื่น (R+G+B)/3
   │
   ├─ LCD  : drawSpectrum + drawColorBar + drawInfo + MENU/PREDICT buttons
   │         (MENU → calibration list: axis/dark/order/lamp/wavelength)
   ├─ Web  : /data (JSON) + /capture (JPEG)
   ├─ CSV  : /record?conc=.. → LittleFS /dataset.csv
   └─ ML   : /dataset.csv → ml/train_plsr.py → model_weights.h → predictConcentration()
```

หลักการสำคัญ:
- ข้อมูล R/G/B ดิบไม่ถูก clamp; clamp เฉพาะพิกัดที่วาดบนจอ
- แกน X เป็น **nm** จาก runtime N-point calibration (piecewise-linear) หรือ 2-point fallback `CAL_*`
- ค่า intensity ที่ใช้ทั้งกราฟ/CSV/โมเดล คือค่าเฉลี่ยเท่ากัน `(R+G+B)/3` (สลับโหมดได้ด้วย `COMBINE_MODE`)
- ลำดับ row ถูก flip runtime ให้ index 0 = ปลายน้ำเงิน (380 nm) เสมอ; ค่าเริ่มต้นคือ `SPECTRUM_INVERT` แต่ auto-detect/เว็บปรับได้
- สีของกราฟ/color bar อิง `wavelengthToRGB()` (cosmetic)

## 3. ฟีเจอร์ที่ทำแล้ว

- [x] อ่านภาพจากกล้อง RGB565 VGA
- [x] ดึงสเปกตรัมจากคอลัมน์กลาง (แนวตั้ง) ± ช่วงเฉลี่ย
- [x] Dark frame capture (BOOT ค้าง) + แสดงสถานะบนจอ
- [x] Temporal averaging + moving average
- [x] แยก R/G/B ตลอด pipeline แล้วรวมด้วยค่าเฉลี่ยเท่ากันเพื่อแสดงผล/บันทึก (Rec.709/Rec.601 เลือกได้แต่เป็น perceptual luma)
- [x] กราฟ intensity vs wavelength (nm) บน LCD + color bar + peak/max/FPS
- [x] WiFi AP (`ITIM-project`) + WebServer
- [x] หน้าเว็บแสดงกราฟ + ภาพกล้อง + เส้น/แถบแนวตั้งบอกคอลัมน์ที่วัด
- [x] 2-point wavelength calibration (`CAL_PIXEL_*`, `CAL_NM_*`) เป็น fallback
- [x] Runtime N-point wavelength calibration: หา peak อัตโนมัติ/กรอกจุดเอง (px→nm) แล้ว fit piecewise-linear ผ่านทุกจุด สูงสุด 8 จุด + NVS
- [x] เก็บ dataset CSV ลง flash (LittleFS หรือ FFat ตาม partition) ผ่านเว็บ + ดาวน์โหลด/ล้าง
- [x] PLSR training pipeline (`ml/`) + data augmentation + export header
- [x] On-device inference (dot product) + แสดงผลบน LCD และเว็บ
- [x] ปุ่ม PREDICT บนจอ (touch) + BOOT สำรอง
- [x] ปุ่ม **MENU** บนจอ → เมนู calibrate ครบชุดโดยไม่ต้องเปิดเว็บ: axis calibrate, dark frame, flip/auto row order, เลือกหลอด reference (วนด้วยการกดซ้ำ), calibrate wavelength จาก preset, back
- [x] Wavelength calibration จาก preset หลอด (Fluorescent/Hydrogen/Neon/Cadmium) — จับคู่ peak กับเส้นหลอดด้วย affine RANSAC (ไม่ต้องพึ่ง mapping เดิม) ต้องได้ ≥3 row และ span ≥50 nm; สำเร็จแล้วเก็บ NVS `cal` เหมือนทางเว็บ
- [x] Auto center-line: fit `x(y)=a*y+b` ตามแนวสเปกตรัม (แก้ tilt/offset), **ไม่ track เอง** — ขยับเฉพาะตอน calibrate
- [x] Axis calibration (ปุ่มที่สาม/`/calibrate`/serial `c`) → **lock + เก็บ NVS**, `/axis/auto` = fit เฟรมเดียวแล้ว lock
- [x] แถบ **Axis trim** บนหน้าเว็บ: ขยับ tilt `a` / offset `b` เอง (`/axis/nudge`, `/axis/set`, `/axis/reset`, `/axis/save`) แล้ว lock + เก็บ NVS (หน่วง 1.5 s กันเขียน flash ถี่)
- [x] เส้นเหลืองฝังในภาพ `/capture` จากเฟรมเดียวกับที่วัด (ไม่มี overlay ล่าช้าบนเว็บ)
- [x] Captive portal + mDNS (`http://spectrometer.local`) เข้าเว็บได้ทันทีหลังต่อ WiFi
- [x] Serial commands: `c` (axis calibrate), `a` (auto align), `d` (dark frame), `p` (predict), `i` (flip row order), `m` (เปิด/ปิดเมนู calibrate บนจอ), `w` (รายงาน calibration + peak)
- [x] Auto/สลับลำดับสเปกตรัม: `SPECTRUM_AUTO_ORIENT`, `/spectrum/flip`, `/spectrum/orient`, serial `i` + เก็บลง NVS (`inv`)
- [x] แยกแสงหักเหข้างแถบหลักออกด้วย half-max midpoint + prediction window (±`AXIS_PRED_WIN`)
- [x] `fitSpectrumAxis()` ใช้ shear projection (fit เส้นตรงทั้งภาพ: ลองความชัน `a` แล้วเลือกที่ profile คมสุด) + refine รายแถว; ถ้าไม่เจอจะ fallback ไปวิธี walk เดิม (ทดสอบ offline กับเฟรมสังเคราะห์แล้ว ยังไม่ได้ทดสอบบนบอร์ด)
- [x] `SPECTRUM_INVERT` เป็นค่าเริ่มต้นตอนบูต (runtime เปลี่ยนได้); axis detector ใช้ max(R,G,B) แทน luma เพื่อไม่ให้แถบน้ำเงินจางหาย
- [x] เว็บกราฟเฉลี่ยเป็น bin (`WEB_SPECTRUM_POINTS` 160 จุด) แทนการสุ่มหยิบจุดเดียว
- [x] กล้องใช้ saturation 0 (เดิม -2 ทำให้ ISP ผสม R/G/B เข้าหากัน)
- [x] รีเฟรชทุก 300 ms (`FRAME_INTERVAL_MS`)

## 4. ค่าคอนฟิกหลัก (`spectrometer/spectrometer.ino`)

| ค่า | ปัจจุบัน | หมายเหตุ |
|---|---|---|
| `CENTER_COL` | 320 | fallback เมื่อยัง detect แนวไม่ได้ |
| `AXIS_AUTO_DETECT` | 0 | 0 = เส้นขยับเฉพาะตอน calibrate |
| `AXIS_MAX_SLOPE` | 0.30 | จำกัดความเอียงสูงสุด (px/row) |
| `AXIS_EMA_ALPHA` | 0.25 | ความลื่นของเส้นแนวสเปกตรัม (โหมด auto) |
| `AXIS_PRED_WIN` | 24 px | หน้าต่างค้นหารอบตำแหน่งที่ทำนาย |
| `AXIS_SELECT_FRACTION` | 0.80 | กันการกระโดดไป order ข้างเคียง |
| `AXIS_PEAK_FRACTION` | 0.15 | row peak ต้อง ≥ 15% ของ frame peak |
| `DARKFRAME_BUTTON_PIN` | 0 | ปุ่ม action (สั้น=predict, ค้าง=dark) |
| `CAL_BUTTON_PIN` | -1 | ปุ่มที่สามสำหรับ axis calibration (ตั้ง GPIO เมื่อต่อปุ่ม) |
| `COL_AVG_RANGE` | 10 | เฉลี่ย ±10 คอลัมน์ |
| `SPECTRUM_LEN` | 480 | = FRAME_HEIGHT |
| `SPECTRUM_INVERT` | 0 | ค่าเริ่มต้นตอนบูต; runtime auto-detect/สลับได้ |
| `SPECTRUM_AUTO_ORIENT` | 1 | 1 = หาปลายน้ำเงินอัตโนมัติจากเฟรมแรกที่ใช้ได้ |
| `TEMPORAL_FRAMES` | 4 | จำนวนเฟรมเฉลี่ย |
| `SMOOTH_WINDOW` | 7 | moving average |
| `FRAME_INTERVAL_MS` | 300 | คาบอัปเดต |
| `COMBINE_MODE` | `COMBINE_AVERAGE` | equal-weight `(R+G+B)/3`; Rec.709/Rec.601 เป็น legacy |
| `COMBINE_GAIN_R/G/B` | 1.0 (ปิด) | trim ราย channel หลังวัด white reference (ต้องเทรนใหม่) |
| `WEB_SPECTRUM_POINTS` | 160 | จำนวนจุดกราฟเว็บ (เฉลี่ยเป็น bin) |
| `CAL_PIXEL_1/2` | 0 / 479 | fallback 2-point (เต็มคอลัมน์) |
| `CAL_NM_1/2` | 380 / 700 nm | fallback 2-point (visible) |
| `CAL_MAX_POINTS` | 8 | จำนวนจุดสูงสุดของ runtime calibration |
| `SPEC_NM_MIN/MAX` | 380 / 700 nm | clamp ผล `pixelToNm()` |
| `MANUAL_EXPOSURE` | 300 | ต้อง tune กับแสงจริง |
| `MANUAL_GAIN` | 0 | ต้อง tune กับแสงจริง |
| `DISPLAY_MAX_INTENSITY` | 300 | สเกลจอ (ไม่ใช่ clamp ข้อมูล) |
| `TOUCH_I2C_ADDR` | 0x3B | AXS15231B touch |
| `DARKFRAME_HOLD_MS` | 1500 | กด BOOT ค้างเพื่อ dark frame (ในเมนู = รันรายการที่ไฮไลต์) |
| `LAMP_TOL_ROWS` | 4.0 | ระยะ row ที่ยอมรับตอนจับคู่เส้นหลอด reference |
| `LAMP_MERGE_NM` | 3.5 | เส้นที่ห่างกันน้อยกว่านี้ใช้ peak เดียวกันได้ (577/579) |
| `LAMP_MIN_SPAN_NM` | 50 | ช่วง nm ต่ำสุดของจุดที่จับคู่ได้ ก่อนยอมรับ calibration |
| `CSV_POINTS` (`dataset.h`) | 120 | ต้องเท่ากับ `MODEL_INPUT_DIM` |

## 5. Hardware / Pins

- LCD QSPI: CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
- Camera: XCLK=38, SIOD=8, SIOC=7, D7..D0=21,39,40,42,46,48,47,45, VSYNC=17, HREF=18, PCLK=41
- I2C (TCA9554 + touch): SDA=8, SCL=7 (แชร์กับ camera SCCB)
- BOOT: GPIO0

Board settings: ESP32S3 Dev Module, OPI PSRAM, USB CDC On Boot = Enabled
Partition scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** หรือ **16M Flash (3MB APP/9.9MB FATFS)** (โค้ดตรวจและ mount เองอัตโนมัติ)

## 6. Build / Flash

1. เปิด `spectrometer/spectrometer.ino` ใน Arduino IDE
2. ตั้ง board ตามข้อ 5
3. Verify / Upload
4. เชื่อม WiFi `ITIM-project` / `itimsarapark` → เปิด `http://192.168.4.1`
5. กด BOOT ค้าง >= 1.5 s เพื่อจับ dark frame (ปิดช่องรับแสงก่อน)

## 7. ML workflow

```bash
# บนเครื่อง PC
cd ml
python -m pip install -r requirements.txt
python train_plsr.py --csv dataset.csv --unit "%"
# คัดลอก spectrometer/model_weights.h แล้ว rebuild/flash
```

รายละเอียด: `ml/README.md`

## 8. Known issues

- ค่า fallback ยัง map เต็มคอลัมน์เป็น 380–700 nm — ใช้หน้า **Wavelength calibration** วัด 405/546 nm จากหลอดจริงแล้ว Apply (เก็บ NVS, ไม่ต้อง reflash)
- Calibration points ต้องอยู่ใน 380–700 nm และเรียง px/nm ขึ้น; ต้องแก้ลำดับ row (blue→red) ก่อน calibrate
- การ auto-detect ลำดับ row ต้องมีทั้งปลายแดงและน้ำเงินในเฟรม; แหล่งกำเนิด monochromatic (เช่น LED เขียว) จะคงลำดับเดิมและรายงาน `orientKnown=false`
- Touch: ย้ายไป bus **8/7 + Waveshare BSP** (`bsp_touch_init`) แล้ว — รอทดสอบบนฮาร์ดแวร์จริง
- Axis calibration เก็บใน NVS (`Preferences` namespace `axis`) → คงอยู่ข้าม reboot; ถ้าย้ายแท่น/เปลี่ยนแสงต้อง calibrate ใหม่
- **ปุ่มกายภาพยังไม่ผูกครบ**: `DARKFRAME_BUTTON_PIN = 0` (BOOT) และ `CAL_BUTTON_PIN = -1`; ต้องตั้งเมื่อรู้ GPIO จริง
- ใช้ `/calibrate` หรือ serial `c` แทนปุ่ม calibrate ได้
- Wavelength calibration จากเมนูบนจอ (preset หลอด) ต้องมีเส้นหลอดจริง ≥3 เส้นและกระจาย ≥50 nm ไม่งั้นจะปฏิเสธและคงค่าเดิม; ชุดที่บรรจุไว้คือ Fluorescent, Hydrogen, Neon, Cadmium — หลอดอื่นต้องใช้หน้าเว็บ (`/cal/set`) หรือเพิ่ม preset ในโค้ด
- `/capture` ดึงเฟรมใหม่แยกจาก loop → บางรอบ measurement frame ถูกใช้ไป
- อัตราเฟรมจำกัดจาก canvas flush + JPEG conversion
- ยังไม่ init PMU (AXP2101); บาง revision อาจต้องทำก่อนใช้กล้อง
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ยังไม่ tune กับแสงจริง
- เปลี่ยน exposure/gain/calibration/COMBINE_MODE/COMBINE_GAIN แล้ว **ต้องเก็บ dataset ใหม่และเทรนใหม่**
- `test/` เป็น reference เก่า ห้ามใช้ build หลัก

## 8.1 การเข้าเว็บ / storage

- ต่อ WiFi `ITIM-project` แล้วหน้าเว็บจะเด้งเอง (captive portal) หรือพิมพ์ `http://192.168.4.1` / `http://spectrometer.local`
- Dataset ตรวจ partition อัตโนมัติ: LittleFS (SPIFFS) หรือ FFat (FATFS) — ดู backend ที่ log ตอนบูต

## 9. Roadmap

1. **Calibration จริง** — วัด peak 405/546 nm แล้วกรอกผ่านหน้า Wavelength calibration (NVS, ไม่ต้อง reflash), ตรวจกับหลอดอื่น
2. **ปรับ exposure/gain จากเว็บ** — ลดรอบ reflash ตอน tune
3. **Dataset management** — เพิ่มการดูรายการ sample / ลบทีละแถว
4. **โมเดลทางเลือก** — ถ้าความสัมพันธ์ไม่เป็นเชิงเส้น เปลี่ยนเป็น MLP (interface เดิม)
5. **บันทึกผลอัตโนมัติ** — log prediction พร้อม timestamp
6. **Fe analysis** — ต่อยอดจากข้อมูล R/G/B ที่แยกไว้

## 10. ประวัติการเปลี่ยนโครงสร้าง

- รวมโค้ดทดลองจาก `files (4)/` เป็น sketch หลักที่ `spectrometer/`
- เปลี่ยนการอ่านจากแถวกลาง (แนวนอน) เป็น **คอลัมน์กลาง (แนวตั้ง)**
- เพิ่ม overlay เส้นแนวตั้ง + แถบบนภาพเว็บ
- ลดคาบอัปเดต 1000 ms → 300 ms
- เปลี่ยนการรวมสีเป็น Rec.709 luma
- เพิ่ม 2-point calibration, dataset LittleFS, PLSR pipeline และ on-device inference
- เพิ่ม FFat backend, captive portal, mDNS, serial commands
- เพิ่ม auto center-line (track แถบหลัก + linear fit) และ axis calibration
- แก้ stack overflow (canary) ด้วยการย้าย buffer ใหญ่เป็น static
- ลบ `final/` (เก็บประวัติใน git)
- เปลี่ยน combine เป็น equal-weight average + auto/runtime row order (NVS `inv`) + axis detector ใช้ max channel + saturation 0
- เว็บกราฟเปลี่ยนเป็น bin average 160 จุด; dark frame หารด้วยจำนวนเฟรมที่สำเร็จจริง
- เพิ่ม runtime N-point wavelength calibration (peak finder + manual point + NVS `cal`) และกราฟใช้ mapping เดียวกับ firmware
- เพิ่มเมนู calibrate บนจอ (ปุ่ม MENU, touch หรือ BOOT navigate) พร้อม preset หลอด reference + matcher แบบ affine RANSAC — calibrate ได้แม้เปิดเว็บไม่ได้
