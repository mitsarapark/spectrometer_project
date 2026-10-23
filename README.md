# spectrometer_project

เครื่อง spectrometer แบบพกพาบน **ESP32-S3-Touch-LCD-3.5B** (Waveshare)
อ่านสเปกตรัมจากกล้อง onboard ที่ผ่าน grating (กระจายแนวตั้ง) จาก **คอลัมน์กลาง** ของภาพ
แล้ววาดกราฟ intensity vs pixel position บนจอ LCD พร้อมเปิด WiFi AP ให้ดูภาพกล้อง + กราฟผ่านเว็บ

## เอกสาร

- [`AGENTS.md`](AGENTS.md) — คู่มือสำหรับ AI agent / ผู้พัฒนาต่อ
- [`PROJECT_STATUS.md`](PROJECT_STATUS.md) — สถานะงาน, ค่าคอนฟิก, known issues, roadmap

## โครงสร้าง

```
spectrometer/            sketch หลัก
  spectrometer.ino       setup/loop, camera, WiFi/WebServer, HTML, pipeline
  spectrum_processing.h  extraction, dark frame, averaging, combine
  display.h              วาดกราฟ/แกน/ข้อมูลลง LCD
final/, test/            โค้ดเก่า/ทดสอบ (reference เท่านั้น)
```

## ฟีเจอร์

- Dark frame capture (กดปุ่ม BOOT)
- อ่านสเปกตรัมแนวตั้งจากคอลัมน์กลาง ± ช่วงเฉลี่ย
- Temporal averaging + moving average, เก็บ R/G/B แยก channel
- กราฟ + color bar + peak/max/FPS บน LCD
- WiFi AP + หน้าเว็บแสดงกราฟและภาพกล้อง พร้อมเส้น/แถบแนวตั้งบอกตำแหน่งที่วัด
- `SPECTRUM_INVERT` สำหรับกลับลำดับสเปกตรัม

## Build / Flash

1. เปิด `spectrometer/spectrometer.ino` ใน Arduino IDE
2. Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Partition: **Huge APP (3MB No OTA/1MB SPIFFS)**
   - USB CDC On Boot: **Enabled**
3. Libraries: `Arduino_GFX`, `TCA9554`, `esp32-camera` (มากับ ESP32 core)
4. Verify / Upload
5. ต่อ WiFi `ITIM-project` (password `itimsarapark`) แล้วเปิด `http://192.168.4.1`

## หมายเหตุ

- แกน X ยังเป็น pixel index (ยังไม่ calibrated เป็น nm)
- สีของกราฟเป็นเพียง cosmetic
- ค่าข้อมูลดิบไม่ถูก clamp; clamp เฉพาะตอนวาดบนจอ
