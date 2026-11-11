# PROJECT STATUS — ESP32-S3 Spectrometer

อัปเดตล่าสุด: 2026-11-11

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
   ├─ extractSpectrum()              อ่านตามแนว x(y) ± COL_AVG_RANGE → R/G/B ต่อ row
   ├─ applyDarkFrameSubtraction()    ลบ baseline (ถ้าจับ dark frame แล้ว)
   ├─ applyTemporalAverage()         เฉลี่ย 4 เฟรมล่าสุด
   ├─ applyMovingAverage()           smooth window 7
   ├─ combineForDisplay()            Rec.709 luma (0.2126R + 0.7152G + 0.0722B)
   │
   ├─ LCD  : drawSpectrum + drawColorBar + drawInfo + PREDICT button
   ├─ Web  : /data (JSON) + /capture (JPEG)
   ├─ CSV  : /record?conc=.. → LittleFS /dataset.csv
   └─ ML   : /dataset.csv → ml/train_plsr.py → model_weights.h → predictConcentration()
```

หลักการสำคัญ:
- ข้อมูล R/G/B ดิบไม่ถูก clamp; clamp เฉพาะพิกัดที่วาดบนจอ
- แกน X เป็น **nm** จาก 2-point calibration (ค่าเริ่มต้นเป็น placeholder)
- ค่า intensity ที่ใช้ทั้งกราฟ/CSV/โมเดล คือ Rec.709 luma (สลับโหมดได้ด้วย `COMBINE_MODE`)
- สีของกราฟ/color bar อิง `wavelengthToRGB()` (cosmetic)

## 3. ฟีเจอร์ที่ทำแล้ว

- [x] อ่านภาพจากกล้อง RGB565 VGA
- [x] ดึงสเปกตรัมจากคอลัมน์กลาง (แนวตั้ง) ± ช่วงเฉลี่ย
- [x] Dark frame capture (BOOT ค้าง) + แสดงสถานะบนจอ
- [x] Temporal averaging + moving average
- [x] แยก R/G/B ตลอด pipeline แล้วรวมเป็น Rec.709 เพื่อแสดงผล/บันทึก
- [x] กราฟ intensity vs wavelength (nm) บน LCD + color bar + peak/max/FPS
- [x] WiFi AP (`ITIM-project`) + WebServer
- [x] หน้าเว็บแสดงกราฟ + ภาพกล้อง + เส้น/แถบแนวตั้งบอกคอลัมน์ที่วัด
- [x] 2-point wavelength calibration (`CAL_PIXEL_*`, `CAL_NM_*`)
- [x] เก็บ dataset CSV ลง flash (LittleFS หรือ FFat ตาม partition) ผ่านเว็บ + ดาวน์โหลด/ล้าง
- [x] PLSR training pipeline (`ml/`) + data augmentation + export header
- [x] On-device inference (dot product) + แสดงผลบน LCD และเว็บ
- [x] ปุ่ม PREDICT บนจอ (touch) + BOOT สำรอง
- [x] Auto center-line: fit `x(y)=a*y+b` ตามแนวสเปกตรัม (แก้ tilt/offset) + overlay เอียงตามจริงบนเว็บ
- [x] Axis calibration (ปุ่มที่สามหรือ `/calibrate`) สำหรับล็อกแนว/ความเอียง
- [x] Captive portal + mDNS (`http://spectrometer.local`) เข้าเว็บได้ทันทีหลังต่อ WiFi
- [x] Serial commands: `c` (axis calibrate), `d` (dark frame), `p` (predict)
- [x] แยกแสงหักเหข้างแถบหลักออกด้วย tracking window (±`AXIS_TRACK_WIN`)
- [x] `SPECTRUM_INVERT` สำหรับกลับลำดับ row
- [x] รีเฟรชทุก 300 ms (`FRAME_INTERVAL_MS`)

## 4. ค่าคอนฟิกหลัก (`spectrometer/spectrometer.ino`)

| ค่า | ปัจจุบัน | หมายเหตุ |
|---|---|---|
| `CENTER_COL` | 320 | fallback เมื่อยัง detect แนวไม่ได้ |
| `AXIS_AUTO_DETECT` | 1 | เปิด auto tilt/offset detection |
| `AXIS_MAX_SLOPE` | 0.30 | จำกัดความเอียงสูงสุด (px/row) |
| `AXIS_EMA_ALPHA` | 0.25 | ความลื่นของเส้นแนวสเปกตรัม |
| `AXIS_TRACK_WIN` | 30 px | หน้าต่าง track รอบแถวก่อนหน้า |
| `AXIS_REFINE_WIN` | 8 px | หน้าต่าง centroid รอบ peak |
| `AXIS_PEAK_FRACTION` | 0.5 | row peak ต้อง ≥ ครึ่งของ frame peak |
| `DARKFRAME_BUTTON_PIN` | 0 | ปุ่ม action (สั้น=predict, ค้าง=dark) |
| `CAL_BUTTON_PIN` | -1 | ปุ่มที่สามสำหรับ axis calibration (ตั้ง GPIO เมื่อต่อปุ่ม) |
| `COL_AVG_RANGE` | 10 | เฉลี่ย ±10 คอลัมน์ |
| `SPECTRUM_LEN` | 480 | = FRAME_HEIGHT |
| `SPECTRUM_INVERT` | 0 | ตั้ง 1 ถ้าสเปกตรัมกลับหัว |
| `TEMPORAL_FRAMES` | 4 | จำนวนเฟรมเฉลี่ย |
| `SMOOTH_WINDOW` | 7 | moving average |
| `FRAME_INTERVAL_MS` | 300 | คาบอัปเดต |
| `COMBINE_MODE` | `COMBINE_REC709` | Rec.709 / Rec.601 / average |
| `CAL_PIXEL_1/2` | 0 / 479 | default เต็มคอลัมน์ |
| `CAL_NM_1/2` | 380 / 700 nm | ช่วง visible; แก้เป็นค่าที่วัดจริงได้ |
| `SPEC_NM_MIN/MAX` | 380 / 700 nm | clamp ผล `pixelToNm()` |
| `MANUAL_EXPOSURE` | 300 | ต้อง tune กับแสงจริง |
| `MANUAL_GAIN` | 0 | ต้อง tune กับแสงจริง |
| `DISPLAY_MAX_INTENSITY` | 300 | สเกลจอ (ไม่ใช่ clamp ข้อมูล) |
| `TOUCH_I2C_ADDR` | 0x3B | AXS15231B touch |
| `DARKFRAME_HOLD_MS` | 1500 | กด BOOT ค้างเพื่อ dark frame |
| `CSV_POINTS` (`dataset.h`) | 120 | ต้องเท่ากับ `MODEL_INPUT_DIM` |

## 5. Hardware / Pins

- LCD QSPI: CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
- Camera: XCLK=38, SIOD=8, SIOC=7, D7..D0=21,39,40,42,46,48,47,45, VSYNC=17, HREF=18, PCLK=41
- I2C (TCA9554 + touch): SDA=21, SCL=22
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

- ค่า calibration เริ่มต้น map เต็มคอลัมน์เป็น 380–700 nm — ควรวัด 405/546 nm จากหลอด fluorescent จริงแล้วแก้ `CAL_*`
- Touch (`0x3B`) ยังไม่ยืนยันบนฮาร์ดแวร์; ถ้าไม่เจอจะ fallback ไป BOOT
- **ปุ่มกายภาพยังไม่ผูกครบ**: `DARKFRAME_BUTTON_PIN = 0` (BOOT) และ `CAL_BUTTON_PIN = -1`; ต้องตั้งเมื่อรู้ GPIO จริง
- ใช้ `/calibrate` หรือ serial `c` แทนปุ่ม calibrate ได้
- `/capture` ดึงเฟรมใหม่แยกจาก loop → บางรอบ measurement frame ถูกใช้ไป
- อัตราเฟรมจำกัดจาก canvas flush + JPEG conversion
- ยังไม่ init PMU (AXP2101); บาง revision อาจต้องทำก่อนใช้กล้อง
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ยังไม่ tune กับแสงจริง
- เปลี่ยน exposure/gain/calibration/COMBINE_MODE แล้ว **ต้องเก็บ dataset ใหม่และเทรนใหม่**
- `test/` เป็น reference เก่า ห้ามใช้ build หลัก

## 8.1 การเข้าเว็บ / storage

- ต่อ WiFi `ITIM-project` แล้วหน้าเว็บจะเด้งเอง (captive portal) หรือพิมพ์ `http://192.168.4.1` / `http://spectrometer.local`
- Dataset ตรวจ partition อัตโนมัติ: LittleFS (SPIFFS) หรือ FFat (FATFS) — ดู backend ที่ log ตอนบูต

## 9. Roadmap

1. **Calibration จริง** — วัด peak 405/546 nm แล้วแก้ `CAL_*`, ตรวจสอบกับหลอดอื่น
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
