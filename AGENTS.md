# AGENTS.md

คู่มือสำหรับ AI agent / ผู้พัฒนาที่กลับมาทำงานต่อในโปรเจกต์นี้ (อ่านก่อนแก้โค้ดทุกครั้ง)

## 1. เป้าหมายโปรเจกต์

สร้างเครื่อง spectrometer แบบพกพาบนบอร์ด **ESP32-S3-Touch-LCD-3.5B** (Waveshare)
โดยใช้กล้อง onboard อ่านสเปกตรัมแสงที่ผ่าน grating แล้ว:

1. มีฟังก์ชัน **dark frame capture**
2. ดึงสเปกตรัมจาก **คอลัมน์กลาง** ของภาพ (สเปกตรัมกระจายแนวตั้ง)
3. วาดกราฟ intensity vs wavelength (nm) ลงจอ LCD ในตัวเครื่อง
4. เปิด WiFi AP ให้เว็บเบราว์เซอร์ดู **ภาพจากกล้อง + กราฟ** พร้อมเส้น/แถบแนวตั้งบอกตำแหน่งที่วัด
5. เก็บ dataset ลง flash ผ่านเว็บ เพื่อนำไปเทรน PLSR บน PC
6. รันโมเดลบน ESP32 ทำนายความเข้มข้นของสารเมื่อกดปุ่มบนจอ
7. เปิด captive portal + mDNS (`http://spectrometer.local`) ให้เข้าเว็บได้ง่าย

## 2. โครงสร้างไฟล์

```
spectrometer/                 <-- sketch หลัก (ชื่อโฟลเดอร์ต้องตรงกับ .ino)
  spectrometer.ino            setup/loop, camera, WiFi/WebServer, HTML, pipeline orchestration
  spectrum_processing.h       extraction, dark frame, averaging, combine, calibration
  display.h                   วาดกราฟ/แกน/color bar/ข้อมูล/ปุ่ม PREDICT ลง LCD
  dataset.h                   บันทึก CSV ลง LittleFS + นับ sample + clear
  inference.h                 รัน PLS dot product บนเครื่อง
  model_weights.h             โมเดลที่ generate จาก ml/ (placeholder = MODEL_READY 0)
  touch.h                     อ่าน touch AXS15231B (fallback ไป BOOT ถ้าไม่เจอ)
ml/                           เทรน PLSR บน PC
  train_plsr.py               โหลด CSV -> augment -> CV -> export model_weights.h
  augment.py                  เพิ่ม noise / baseline / gain drift
  README.md                   ขั้นตอนเก็บข้อมูล + เทรน + flash
test/                         โค้ดทดสอบ display/camera (reference เท่านั้น)
PROJECT_STATUS.md             สถานะงาน + roadmap
README.md                     ภาพรวม + วิธี build/flash
```

หมายเหตุ: โฟลเดอร์ `final/` ถูกลบแล้ว (ประวัติยังอยู่ใน git) เพื่อกันการสับสนว่าเป็นเวอร์ชันล่าสุด

## 3. Hardware / Board settings

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** (จำเป็น เพราะ frame buffer RGB565 VGA + canvas)
- Partition: **Huge APP (3MB No OTA/1MB SPIFFS)** หรือ **16M Flash (3MB APP/9.9MB FATFS)** — ทั้งสองแบบใช้ได้ (LittleFS หรือ FFat ตาม partition ที่พบ)
- USB CDC On Boot: **Enabled** (ถ้าต้องการ Serial)
- Libraries: `esp32-camera` (มากับ ESP32 core), `Arduino_GFX` (moononournation), `TCA9554`

### Pin map ที่ใช้จริง (ห้ามเดา ใช้ค่านี้)

LCD QSPI: CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
Camera: XCLK=38, SIOD=8, SIOC=7, D7..D0 = 21,39,40,42,46,48,47,45,
VSYNC=17, HREF=18, PCLK=41, PWDN=-1, RESET=-1
I2C (TCA9554 + touch): SDA=21, SCL=22
Touch controller: AXS15231B @ I2C `0x3B`
BOOT button: GPIO0 (INPUT_PULLUP, active LOW)

## 4. Data flow

```
esp_camera_fb_get() [RGB565 VGA 640x480]
  -> detectCenterLine()       fit x(y) = a*y + b on the brightest column per row
  -> extractSpectrum()        center column +/- COL_AVG_RANGE **along that line**
  -> applyDarkFrameSubtraction()
  -> applyTemporalAverage()   TEMPORAL_FRAMES frames
  -> applyMovingAverage()     SMOOTH_WINDOW
  -> combineForDisplay()      Rec.709 luma (0.2126R + 0.7152G + 0.0722B)
  -> drawSpectrum()/drawColorBar()/drawInfo()/drawPredictButton() -> gfx->flush()
  -> /data JSON (80 จุด + nm + model/sample metadata) + /capture JPEG
  -> /record?conc=.. (LittleFS CSV) -> /dataset.csv -> ml/train_plsr.py
  -> predictConcentration(): dot product บน spectrumCombined
```

สำคัญ:
- **ห้าม clamp ค่าข้อมูลจริง** ที่ 255; clamp เฉพาะตอน map เป็นพิกัด pixel บนจอ
- ค่า RGB ถูกเก็บแยก channel ตลอด pipeline; `combineForDisplay()` ใช้เฉพาะกราฟ/CSV/โมเดล
- แกน X เป็น nm จาก 2-point calibration (ค่าเริ่มต้นเป็น placeholder)
- `SPECTRUM_INVERT` กลับลำดับ row ถ้าภาพสเปกตรัมกลับหัว
- `CSV_POINTS` ต้องเท่ากับ `MODEL_INPUT_DIM` (มี `#error` ตรวจให้แล้ว)

## 5. การ build / flash

เครื่อง develop อาจไม่มี `arduino-cli` หรือ `pio` ติดตั้ง (ตรวจก่อนใช้)
ถ้าไม่มี ให้ผู้ใช้ compile/flash ผ่าน Arduino IDE เอง:

1. เปิด `spectrometer/spectrometer.ino`
2. เลือก board ตามข้อ 3
3. Verify/Upload
4. ต่อ WiFi `ITIM-project` (password `itimsarapark`) แล้วเปิด `http://192.168.4.1`

## 6. การใช้งานปุ่ม

| การกระทำ | ผล |
|---|---|
| แตะปุ่ม **PREDICT** บนจอ | ทำนายความเข้มข้น (ถ้า `MODEL_READY 1`) |
| `BTN_MAIN_PIN` กดสั้น (< 1.5 s) | ทำนายความเข้มข้น |
| `BTN_MAIN_PIN` กดค้าง (>= 1.5 s) | จับ dark frame |
| `BTN_CAL_PIN` กดสั้น | Center-line calibration (ล็อกค่า a, b) |
| `BTN_CAL_PIN` กดค้าง | รายงาน peak สำหรับ wavelength calibration (ยังไม่ apply) |

ค่า default: `BTN_MAIN_PIN = 0` (BOOT), `BTN_CAL_PIN = -1` (ปิดไว้) — ต้องตั้งเป็น GPIO จริงของบอร์ด

## 7. ML workflow (สรุป)

1. วัด sample ที่ทราบความเข้มข้น → หน้าเว็บกรอกค่า → **Record sample**
2. ทำซ้ำหลายความเข้มข้น (แนะนำ >= 20 samples)
3. **Download CSV** → `ml/train_plsr.py --csv dataset.csv --unit "<หน่วย>"`
4. คัดลอก `model_weights.h` ที่ได้ไปทับใน `spectrometer/`
5. Rebuild + flash → ปุ่ม PREDICT ใช้งานได้

รายละเอียดอยู่ใน `ml/README.md` และ `PROJECT_STATUS.md`

## 8. กติกาการทำงาน (สำคัญ)

- **ทุกครั้งที่มีการแก้ไขไฟล์ ให้ commit ด้วย git ทันที** หลังแก้น้อยๆ เสร็จและตรวจแล้ว
- ก่อน commit: `git status`, `git diff`, ตรวจว่าไม่มี secret
- ข้อความ commit สั้น ตรงงาน ใช้ prefix `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`
- ห้าม commit/push ขึ้น remote เอง เว้นแต่ผู้ใช้สั่ง
- หลีกเลี่ยง comment ที่ไม่จำเป็น; โค้ดมี comment อธิบายส่วนสำคัญอยู่แล้ว
- ถ้าไม่แน่ใจเรื่อง hardware/tuning ให้ถามผู้ใช้ก่อนเดา

## 9. Known issues / gotchas

- **ค่า calibration เริ่มต้น** map ทั้งคอลัมน์เป็น 380–700 nm (row 0 = 380 nm, row 479 = 700 nm) ผ่าน `CAL_*`; เมื่อวัดหลอด fluorescent จริงแล้วให้แก้เป็น row ของ peak 405/546 nm
- `SPEC_NM_MIN/MAX` (380/700) ใช้ clamp ผลของ `pixelToNm()` ไม่ให้หลุดช่วงที่วัด
- **Touch ยังไม่ยืนยันบนฮาร์ดแวร์จริง**: โค้ดสแกน `0x3B` บน Wire (SDA=21/SCL=22) ถ้าไม่เจอจะ fallback ไป BOOT อัตโนมัติ; ถ้าใช้ไม่ได้ให้ลองสแกน bus 8/7
- `/capture` เรียก `esp_camera_fb_get()` แยกจาก loop ทำให้บางรอบ frame ถูกใช้ไป
- Frame rate จริงจำกัดด้วย canvas flush + JPEG conversion; `FRAME_INTERVAL_MS` = 300 ms
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ต้อง tune ตามแหล่งแสงจริง (default 300 / 0)
- `/record` จะปฏิเสธถ้าข้อมูลสเปกตรัมเก่าเกิน 3 วินาที
- **Stack**: ห้ามประกาศ array ขนาดใหญ่ (~2 KB ขึ้นไป เช่น `[SPECTRUM_LEN]`) บน stack ของ loop task — เคยทำให้เกิด `Stack canary watchpoint triggered (loopTask)`; ใช้ `static` แทน (ดู `problem.md` P8)
- เปลี่ยน exposure/gain/calibration/COMBINE_MODE แล้ว **ต้องเก็บ dataset ใหม่และเทรนใหม่**
- ยังไม่มี wavelength calibration อัตโนมัติ และยังไม่มี Fe analysis

## 10. ขั้นตอนตรวจงานก่อนส่ง

1. ตรวจ `SPECTRUM_LEN`, ขนาด buffer, index bounds ให้ตรงกัน (ปัจจุบัน = FRAME_HEIGHT = 480)
2. ตรวจว่าไม่มี identifier เก่า (`CENTER_ROW`, `ROW_AVG_RANGE`) หลงเหลือ
3. ถ้าแก้ HTML/JS ให้ตรวจว่า `handleData()` ส่ง field ที่ JS ใช้ครบ
   (`peakNm`, `nmStart`, `nmEnd`, `modelReady`, `hasPred`, `pred`, `modelUnit`, `samples`)
4. ถ้าแก้ `model_weights.h` ให้ตรวจ `MODEL_INPUT_DIM` == `CSV_POINTS` == จำนวนคอลัมน์ CSV
5. อัปเดต `PROJECT_STATUS.md` เมื่อเพิ่ม/ลดฟีเจอร์
