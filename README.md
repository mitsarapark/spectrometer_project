# spectrometer_project

เครื่อง spectrometer แบบพกพาบน **ESP32-S3-Touch-LCD-3.5B** (Waveshare)
อ่านสเปกตรัมจากกล้อง onboard ที่ผ่าน grating (กระจายแนวตั้ง) จาก **คอลัมน์กลาง** ของภาพ
วาดกราฟ intensity vs wavelength บนจอ LCD เปิด WiFi AP ให้ดูภาพกล้อง + กราฟผ่านเว็บ
และรันโมเดล PLSR บนเครื่องเพื่อทำนายความเข้มข้นของสาร

## เอกสาร

- [`AGENTS.md`](AGENTS.md) — คู่มือสำหรับ AI agent / ผู้พัฒนาต่อ
- [`PROJECT_STATUS.md`](PROJECT_STATUS.md) — สถานะงาน, ค่าคอนฟิก, known issues, roadmap
- [`ml/README.md`](ml/README.md) — ขั้นตอนเก็บ dataset และเทรนโมเดล
- [`problem.md`](problem.md) — บันทึกปัญหาที่เจอ สาเหตุ และวิธีแก้

## โครงสร้าง

```
spectrometer/            sketch หลัก
  spectrometer.ino       setup/loop, camera, WiFi/WebServer, HTML, pipeline
  spectrum_processing.h  extraction, dark frame, averaging, combine, calibration
  display.h              วาดกราฟ/แกน/ข้อมูล/ปุ่ม MENU+PREDICT และเมนู calibrate ลง LCD
  dataset.h              บันทึก CSV ลง LittleFS
  inference.h            รัน PLS dot product บนเครื่อง
  model_weights.h        โมเดล (placeholder จนกว่าจะเทรน)
  touch.h                อ่าน touch AXS15231B
ml/                      เทรน PLSR บน PC (train_plsr.py, augment.py)
test/                    โค้ดทดสอบ (reference เท่านั้น)
```

## ฟีเจอร์

- Dark frame capture (กด BOOT ค้าง >= 1.5 s)
- อ่านสเปกตรัมแนวตั้งตาม **แนวกลางที่ตรวจจับอัตโนมัติ** `x(y)=a*y+b` (แก้ tilt/offset)
- Temporal averaging + moving average, เก็บ R/G/B แยก channel, รวมด้วยค่าเฉลี่ยเท่ากัน `(R+G+B)/3`
- Auto/สลับลำดับ row ให้กราฟวิ่งน้ำเงิน (380 nm) → แดง (700 nm) ด้วย `/spectrum/flip`, `/spectrum/orient` หรือ serial `i`
- Runtime wavelength calibration: หา peak อัตโนมัติจากหลอด reference แล้วกรอก nm ที่รู้ค่า (2–8 จุด)
  → piecewise-linear px→nm เก็บใน NVS; 2-point `CAL_*` เป็น fallback
- **เมนู calibrate บนจอ** (ปุ่ม MENU ข้าง PREDICT) ใช้ได้แม้เปิดเว็บไม่ได้:
  axis calibrate, dark frame, flip/auto row order, เลือกหลอด reference (แตะซ้ำเพื่อวน),
  calibrate wavelength จาก preset (Fluorescent/Hydrogen/Neon/Cadmium), back
  — BOOT สั้น = เลื่อนรายการ, BOOT ค้าง = รันรายการที่เลือก (serial `m` เปิดเมนูได้)
- กราฟ + color bar + peak (nm)/max/FPS บน LCD
- WiFi AP + หน้าเว็บแสดงกราฟและภาพกล้อง พร้อมเส้น/แถบเอียงตามแนวจริง
- Axis calibration ผ่าน `/calibrate`, serial `c` หรือปุ่มที่สาม (ถ้าต่อ)
- เก็บ dataset CSV ลง flash (LittleFS หรือ FFat) ผ่านเว็บ แล้วดาวน์โหลดไปเทรน
- On-device PLSR inference: แตะปุ่ม PREDICT บนจอ (หรือกด BOOT สั้น)
- Captive portal + mDNS: ต่อ WiFi แล้วหน้าเว็บเด้งเอง หรือใช้ `http://spectrometer.local`

## Build / Flash

1. เปิด `spectrometer/spectrometer.ino` ใน Arduino IDE
2. Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Partition: **Huge APP (3MB No OTA/1MB SPIFFS)** หรือ **16M Flash (3MB APP/9.9MB FATFS)** (โค้ดเลือก backend เอง)
   - USB CDC On Boot: **Enabled**
3. Libraries: `Arduino_GFX`, `TCA9554`, `esp32-camera` (มากับ ESP32 core)
4. Verify / Upload
5. ต่อ WiFi `ITIM-project` (password `itimsarapark`) → หน้าเว็บเด้งเอง (captive portal) หรือเปิด `http://192.168.4.1` / `http://spectrometer.local`

## ML quick start

```bash
cd ml
python -m pip install -r requirements.txt
python train_plsr.py --csv dataset.csv --unit "%"
# คัดลอก spectrometer/model_weights.h แล้ว rebuild/flash
```

## หมายเหตุ

- ค่าเริ่มต้น map ทั้งคอลัมน์เป็น **380–700 nm** (`CAL_*`); calibrate จริงได้จากหน้า **Wavelength calibration**
  (ปิดไฟห้อง เปิดหลอด reference → Find peaks → กรอก nm จริง → Apply) โดยไม่ต้อง reflash
  หรือจาก **เมนูบนจอ** (MENU → Lamp preset → Calibrate wavelength) เมื่อเลือกหลอดจาก preset ไว้แล้ว
- Calibration points ต้องอยู่ใน 380–700 nm และต้องเรียง px/nm ขึ้น; ต้องจัดลำดับ row (blue→red) ให้ถูกก่อน calibrate
- Calibrate จาก preset บนจอต้องจับคู่เส้นหลอดได้ ≥3 เส้นและกระจาย ≥50 nm (เช่น fluorescent 405/436/546/577)
  ถ้าไม่ผ่านจะขึ้นข้อความบนจอและ**คงค่าเดิม**; เมนูเลือกหลอดอื่นได้เฉพาะที่บรรจุไว้ในโค้ด
- Calibration อย่างเดียว **ไม่ต้องเทรนโมเดลใหม่** (feature ยังเป็น pixel index เดิม, เปลี่ยนแค่หัวตาราง nm)
- ค่าข้อมูลดิบไม่ถูก clamp; clamp เฉพาะตอนวาดบนจอ
- เปลี่ยน exposure/gain/calibration/COMBINE_MODE/COMBINE_GAIN แล้วต้องเก็บ dataset ใหม่
