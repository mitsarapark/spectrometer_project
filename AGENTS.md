# AGENTS.md

คู่มือสำหรับ AI agent / ผู้พัฒนาที่กลับมาทำงานต่อในโปรเจกต์นี้ (อ่านก่อนแก้โค้ดทุกครั้ง)

## 1. เป้าหมายโปรเจกต์

สร้างเครื่อง spectrometer แบบพกพาบนบอร์ด **ESP32-S3-Touch-LCD-3.5B** (Waveshare)
โดยใช้กล้อง onboard อ่านสเปกตรัมแสงที่ผ่าน grating แล้ว:

1. มีฟังก์ชัน **dark frame capture** (กดปุ่ม BOOT บนบอร์ด)
2. ดึงสเปกตรัมจาก **คอลัมน์กลาง** ของภาพ (สเปกตรัมกระจายแนวตั้ง)
3. วาดกราฟ intensity vs pixel position ลงจอ LCD ในตัวเครื่อง
4. เปิด WiFi AP ให้เว็บเบราว์เซอร์ดู **ภาพจากกล้อง + กราฟ** พร้อมเส้น/แถบแนวตั้งบอกตำแหน่งที่วัด

## 2. โครงสร้างไฟล์

```
spectrometer/                 <-- sketch หลัก (ชื่อโฟลเดอร์ต้องตรงกับ .ino)
  spectrometer.ino            setup/loop, camera, WiFi/WebServer, HTML, pipeline orchestration
  spectrum_processing.h       extraction, dark frame, temporal/spatial averaging, RGB->combined
  display.h                   วาดกราฟ/แกน/color bar/ข้อมูล ลง LCD
final/                        โค้ดเวอร์ชันเก่า (reference เท่านั้น อย่าแก้)
test/                         โค้ดทดสอบ display/camera (reference เท่านั้น)
PROJECT_STATUS.md             สถานะงาน + roadmap
README.md                     ภาพรวม + วิธี build/flash
```

หมายเหตุ: `files (4)/` ถูกย้ายมาเป็น `spectrometer/` แล้ว (ประวัติอยู่ใน git)

## 3. Hardware / Board settings

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** (จำเป็น เพราะ frame buffer RGB565 VGA + canvas)
- Flash/Partition: **Huge APP (3MB No OTA/1MB SPIFFS)**
- USB CDC On Boot: **Enabled** (ถ้าต้องการ Serial)
- Libraries: `esp32-camera` (มากับ ESP32 core), `Arduino_GFX` (moononournation / GFX_Library_for_Arduino), `TCA9554`

### Pin map ที่ใช้จริง (ห้ามเดา ใช้ค่านี้)

LCD QSPI: CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
Camera: XCLK=38, SIOD=8, SIOC=7, D7..D0 = 21,39,40,42,46,48,47,45,
VSYNC=17, HREF=18, PCLK=41, PWDN=-1, RESET=-1
I2C (TCA9554): SDA=21, SCL=22
Dark frame button: GPIO0 (BOOT, INPUT_PULLUP, active LOW)

## 4. Data flow

```
esp_camera_fb_get() [RGB565 VGA 640x480]
  -> extractSpectrum()        center column +/- COL_AVG_RANGE, per row -> R/G/B arrays
  -> applyDarkFrameSubtraction()
  -> applyTemporalAverage()   TEMPORAL_FRAMES frames
  -> applyMovingAverage()     SMOOTH_WINDOW
  -> combineForDisplay()      (R+G+B)/3 เพื่อวาดกราฟเท่านั้น (ข้อมูล R/G/B ดิบยังอยู่ครบ)
  -> drawSpectrum()/drawColorBar()/drawInfo() -> gfx->flush()
  -> /data JSON (downsample 80 จุด) + /capture JPEG
```

สำคัญ:
- **ห้าม clamp ค่าข้อมูลจริง** ที่ 255; การ clamp เกิดเฉพาะตอน map เป็นพิกัด pixel บนจอ (`DISPLAY_MAX_INTENSITY`)
- ค่า RGB ถูกเก็บแยก channel ตลอด pipeline เพื่อรองรับงานวิเคราะห์ในอนาคต
- ตอนนี้แกน X เป็น **pixel index** ยังไม่ calibrated เป็น nm (สีเป็นเพียง cosmetic)
- `SPECTRUM_INVERT` ใน `spectrometer.ino` ใช้กลับลำดับ row ถ้าภาพสเปกตรัมกลับหัว

## 5. การ build / flash

เครื่อง develop อาจไม่มี `arduino-cli` หรือ `pio` ติดตั้ง (ตรวจก่อนใช้)
ถ้าไม่มี ให้ผู้ใช้ compile/flash ผ่าน Arduino IDE เอง:

1. เปิด `spectrometer/spectrometer.ino`
2. เลือก board ตามข้อ 3
3. Verify/Upload
4. ต่อ WiFi `ITIM-project` (password `itimsarapark`) แล้วเปิด `http://192.168.4.1`

## 6. กติกาการทำงาน (สำคัญ)

- **ทุกครั้งที่มีการแก้ไขไฟล์ ให้ commit ด้วย git ทันที** หลังแก้น้อยๆ เสร็จและตรวจแล้ว
- ก่อน commit: `git status`, `git diff`, ตรวจว่าไม่มี secret
- ข้อความ commit สั้น ตรงงาน ใช้ prefix `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`
- ห้าม commit/push ขึ้น remote เอง เว้นแต่ผู้ใช้สั่ง
- หลีกเลี่ยงการใส่ comment ที่ไม่จำเป็น; โค้ดมี comment อธิบายส่วนสำคัญอยู่แล้ว
- ถ้าไม่แน่ใจเรื่อง hardware/tuning ให้ถามผู้ใช้ก่อนเดา

## 7. Known issues / gotchas

- `/capture` เรียก `esp_camera_fb_get()` แยกจาก loop ทำให้บางรอบ frame ถูกใช้ไป (ยอมรับได้ในเวอร์ชันนี้)
- Frame rate จริงจำกัดด้วย canvas flush + JPEG conversion; `FRAME_INTERVAL_MS` = 300 ms
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ต้อง tune ตามแหล่งแสงจริง (default 300 / 0)
- ยังไม่มี wavelength calibration, ยังไม่มี Fe analysis
- บอร์ด Waveshare บาง revision ต้อง init PMU (AXP2101) ก่อนใช้กล้อง — ดูตัวอย่างใน `test/camera_server/`; เวอร์ชันหลักยังไม่ทำ

## 8. ขั้นตอนตรวจงานก่อนส่ง

1. ตรวจ `SPECTRUM_LEN`, ขนาด buffer, index bounds ให้ตรงกัน (ปัจจุบัน = FRAME_HEIGHT = 480)
2. ตรวจว่าไม่มี identifier เก่า (`CENTER_ROW`, `ROW_AVG_RANGE`) หลงเหลือ
3. ถ้าแก้ HTML/JS ให้ตรวจว่า `handleData()` ส่ง field ที่ JS ใช้ครบ
4. อัปเดต `PROJECT_STATUS.md` เมื่อเพิ่ม/ลดฟีเจอร์
