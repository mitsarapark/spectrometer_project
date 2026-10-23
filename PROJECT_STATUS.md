# PROJECT STATUS — ESP32-S3 Spectrometer

อัปเดตล่าสุด: 2026-10-23

## 1. ภาพรวม

เครื่อง spectrometer พกพาบนบอร์ด **Waveshare ESP32-S3-Touch-LCD-3.5B**
ใช้กล้อง onboard จับภาพสเปกตรัมที่กระจายตัว **แนวตั้ง** ผ่าน grating
แล้วอ่านค่าจาก **คอลัมน์กลาง** ของภาพไปวาดกราฟบนจอ LCD
พร้อมเปิด WiFi AP ให้เข้าดูภาพกล้องและกราฟผ่านเว็บเบราว์เซอร์

## 2. สถาปัตยกรรม / Data flow

```
Camera (RGB565 VGA 640x480)
   │
   ├─ extractSpectrum()              center column ± COL_AVG_RANGE → R/G/B ต่อ row
   ├─ applyDarkFrameSubtraction()    ลบ baseline (ถ้ากด BOOT แล้ว)
   ├─ applyTemporalAverage()         เฉลี่ย 4 เฟรมล่าสุด
   ├─ applyMovingAverage()           smooth window 7
   ├─ combineForDisplay()            (R+G+B)/3 สำหรับกราฟเท่านั้น
   │
   ├─ LCD : drawSpectrum + drawColorBar + drawInfo → flush
   └─ Web : /data (JSON 80 จุด) + /capture (JPEG snapshot)
```

หลักการสำคัญ:
- ข้อมูล R/G/B ดิบไม่ถูก clamp; clamp เฉพาะพิกัดที่วาดบนจอ
- แกนกราฟเป็น **pixel index** ยังไม่ calibrated เป็น nm
- สีของกราฟ/color bar เป็น cosmetic (สมมติ 400–700 nm กระจายเต็มแกน)

## 3. ฟีเจอร์ที่ทำแล้ว

- [x] อ่านภาพจากกล้อง RGB565 VGA
- [x] ดึงสเปกตรัมจากคอลัมน์กลาง (แนวตั้ง) ± ช่วงเฉลี่ย
- [x] Dark frame capture ด้วยปุ่ม BOOT (GPIO0) + แสดงสถานะบนจอ
- [x] Temporal averaging + moving average
- [x] แยก R/G/B ตลอด pipeline, combine เพื่อวาดกราฟเท่านั้น
- [x] กราฟ intensity vs pixel position บน LCD + color bar + ข้อมูล peak/max/FPS
- [x] WiFi AP (`ITIM-project`) + WebServer
- [x] หน้าเว็บแสดงกราฟสเปกตรัม + ภาพกล้อง snapshot
- [x] เส้นแนวตั้ง + แถบโปร่งแสงบอกคอลัมน์ที่วัดบนภาพเว็บ
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
| `MANUAL_EXPOSURE` | 300 | ต้อง tune กับแสงจริง |
| `MANUAL_GAIN` | 0 | ต้อง tune กับแสงจริง |
| `DISPLAY_MAX_INTENSITY` | 300 | สเกลจอ (ไม่ใช่ clamp ข้อมูล) |

## 5. Hardware / Pins

- LCD QSPI: CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4, BL=6
- Camera: XCLK=38, SIOD=8, SIOC=7, D7..D0=21,39,40,42,46,48,47,45, VSYNC=17, HREF=18, PCLK=41
- I2C (TCA9554): SDA=21, SCL=22
- BOOT/dark frame: GPIO0

Board settings: ESP32S3 Dev Module, OPI PSRAM, Huge APP (3MB No OTA/1MB SPIFFS), USB CDC On Boot = Enabled

## 6. Build / Flash

1. เปิด `spectrometer/spectrometer.ino` ใน Arduino IDE
2. ตั้ง board ตามข้อ 5
3. Verify / Upload
4. เชื่อม WiFi `ITIM-project` / `itimsarapark` → เปิด `http://192.168.4.1`
5. กด BOOT ค้างเพื่อจับ dark frame (ปิดช่องรับแสงก่อน)

## 7. Known issues

- `/capture` ดึงเฟรมใหม่แยกจาก loop → บางรอบ measurement frame ถูกใช้ไป
- อัตราเฟรมจำกัดจาก canvas flush + JPEG conversion
- ยังไม่ init PMU (AXP2101) ในเวอร์ชันหลัก; บาง revision อาจต้องทำก่อนใช้กล้อง
- `MANUAL_EXPOSURE` / `MANUAL_GAIN` ยังไม่ tune กับแสงจริง
- `final/` และ `test/` เป็น reference เก่า ห้ามใช้ build หลัก

## 8. Roadmap

1. **Wavelength calibration** — 2-point calibration ด้วยหลอด known lines (เช่น 405/546 nm) แล้วแสดงแกน nm
2. **Exposure/gain tuning UI** — ปรับ `MANUAL_EXPOSURE`/`MANUAL_GAIN` โดยไม่ต้อง reflash
3. **ลบ noise/artifact** — ปรับ dark frame หลายเฟรม, ป้องกัน saturation
4. **วิเคราะห์สเปกตรัม** — peak detection หลายจุด, บันทึกข้อมูล CSV ผ่านเว็บ
5. **Fe analysis** — ต่อยอดจากข้อมูล R/G/B ที่แยกไว้
6. **PMU init** — เพิ่ม AXP2101 init ถ้าพบปัญหาเปิดกล้องบนบางบอร์ด

## 9. ประวัติการเปลี่ยนโครงสร้าง

- รวมโค้ดทดลองจาก `files (4)/` เป็น sketch หลักที่ `spectrometer/`
- เปลี่ยนการอ่านจากแถวกลาง (แนวนอน) เป็น **คอลัมน์กลาง (แนวตั้ง)** ตามฮาร์ดแวร์จริง
- เพิ่ม overlay เส้นแนวตั้ง + แถบบนภาพเว็บ
- ลดคาบอัปเดตจาก 1000 ms → 300 ms
