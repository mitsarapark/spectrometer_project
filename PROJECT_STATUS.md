# PROJECT STATUS — ESP32-S3 Spectrometer

อัปเดตล่าสุด: 2026-11-10

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
   ├─ extractSpectrum()              center column ± COL_AVG_RANGE → R/G/B ต่อ row
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
- [x] เก็บ dataset CSV ลง LittleFS ผ่านเว็บ + ดาวน์โหลด/ล้าง
- [x] PLSR training pipeline (`ml/`) + data augmentation + export header
- [x] On-device inference (dot product) + แสดงผลบน LCD และเว็บ
- [x] ปุ่ม PREDICT บนจอ (touch) + BOOT สำรอง
- [x] `SPECTRUM_INVERT` สำหรับกลับลำดับ row
- [x] รีเฟรชทุก 300 ms (`FRAME_INTERVAL_MS`)

## 4. ค่าคอนฟิกหลัก (`spectrometer/spectrometer.ino`)

| ค่า | ปัจจุบัน | หมายเหตุ |
|---|---|---|
| `CENTER_COL` | 320 | คอลัมน์กลางภาพ |
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

Board settings: ESP32S3 Dev Module, OPI PSRAM, Huge APP (3MB No OTA/1MB SPIFFS), USB CDC On Boot = Enabled

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

- ค่า calibration เป็น placeholder — ต้องวัด 405/546 nm จากหลอด fluorescent จริง
- Touch (`0x3B`) ยังไม่ยืนยันบนฮาร์ดแวร์; ถ้าไม่เจอจะ fallback ไป BOOT
- `/capture` ดึงเฟรมใหม่แยกจาก loop → บางรอบ measurement frame ถูกใช้ไป
- อัตราเฟรมจำกัดจาก canvas flush + JPEG conversion
- ยังไม่ init PMU (AXP2101); บาง revision อาจต้องทำก่อนใช้กล้อง
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ยังไม่ tune กับแสงจริง
- ต้องเก็บ dataset ใหม่เมื่อเปลี่ยน exposure/gain/calibration/COMBINE_MODE
- `test/` เป็น reference เก่า ห้ามใช้ build หลัก

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
- ลบ `final/` (เก็บประวัติใน git)
