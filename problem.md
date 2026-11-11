# PROBLEM LOG — ESP32-S3 Spectrometer

ไฟล์นี้บันทึกปัญหาที่เจอระหว่างพัฒนา สาเหตุ และวิธีแก้ เพื่อไม่ต้องไล่ซ้ำเมื่อกลับมาทำต่อ
อัปเดตล่าสุด: 2026-11-11

สถานะ: `OPEN` = ยังไม่จบ, `FIXED` = แก้แล้ว (รอทดสอบบนเครื่อง), `DONE` = ยืนยันแล้ว

---

## P1 — เปิดเว็บ `192.168.4.1` ไม่ได้ / ไม่มี request เข้ามาที่บอร์ด  `FIXED (สาเหตุหลัก = P8)`

> สรุป: request แรก (`PORTAL /hotspot-detect.html`) เข้ามาถึงบอร์ดจริง แต่ทำให้เกิด stack overflow แล้วบอร์ดรีบูต
> หลังรีบูต request ที่พิมพ์ตามมาจึงไม่เคยถูกตอบ ดูรายละเอียดที่ **P8**

**อาการ**
- ต่อ WiFi `ITIM-project` ได้ (`stations=1`) แต่เปิด `http://192.168.4.1` แล้วไม่ขึ้น
- Serial log ไม่มี `GET /` และไม่มี `PORTAL` เลย = request ไม่เคยมาถึงบอร์ด

**สาเหตุที่เป็นไปได้ (ไล่จากเจอจริง)**
1. **`server.begin()` ถูกเรียกหลัง `delay(3000)`** — ในช่วง splash 3 วินาทีแรกยังไม่มีใครฟัง port 80
   ถ้าต่อ WiFi แล้วพิมพ์ IP เร็ว จะได้ `connection refused` ทันที และไม่มี log
2. Browser/OS บังคับ **HTTPS** (Chrome "Always use secure connections", HSTS) → ยิงไป port 443 ซึ่งไม่มีใครฟัง
3. พิมพ์ IP แบบไม่มี `http://` แล้ว browser ตีความเป็น search
4. เครื่องทดสอบต่อ WiFi ตัวอื่น / เปิด VPN / proxy → request ไม่ได้ออกที่ AP
5. มือถือเปิด mobile data ค้างไว้แล้ว route ออก cellular (ควรปิดชั่วคราว)

**สิ่งที่แก้แล้ว**
- ย้าย `server.begin()` + `dnsServer.start()` + `MDNS.begin()` ขึ้นมาก่อน splash, ลด splash 3 s → 1.5 s
- เพิ่ม `WiFi.setSleep(false)`
- เพิ่ม captive portal (DNS wildcard + 302 redirect ทุก path ที่ไม่รู้จัก) → ต่อ WiFi แล้วหน้าเว็บเด้งเอง
- เพิ่ม mDNS `http://spectrometer.local`
- เพิ่ม log: `[ap] client connected`, `PORTAL <uri>`, `GET / from <ip>`, `[443] TLS attempt ...`
- เพิ่ม endpoint `GET /ping` → `pong` สำหรับทดสอบเร็ว
- แสดง URL บนจอ LCD ตอนบูต

**วิธีทดสอบ**
1. Flash แล้วต่อ WiFi `ITIM-project`
2. ดู log ต้องมี `[ap] client connected`
3. เปิด `http://192.168.4.1/`
   - มี `GET / from 192.168.4.x` → request ถึงบอร์ดแล้ว
   - มี `[443]` → browser ใช้ HTTPS ให้พิมพ์ `http://` ให้ชัด หรือปิด secure-connections
   - ไม่มีอะไรเลยแม้แต่ `[ap]` → เครื่องยังไม่ต่อ AP จริง
4. ฝั่ง client: `ipconfig` ต้องได้ `192.168.4.x` และ gateway `192.168.4.1`, `ping 192.168.4.1` ต้องผ่าน

**หมายเหตุ**: port ที่ใช้คือ **80** ไม่ต้องใส่ `:80` ใน URL; ไม่มี HTTPS บนบอร์ด

---

## P2 — `esp_littlefs: Failed to initialize LittleFS`  `FIXED`

**อาการ**
```
E (4311) esp_littlefs: Failed to initialize LittleFS
LittleFS mount failed - dataset disabled
```

**สาเหตุ**
Partition scheme ที่ใช้คือ **16M Flash (3MB APP/9.9MB FATFS)** ซึ่งไม่มี partition ชนิด SPIFFS
จึง mount LittleFS ไม่ได้ (ไม่ใช่บั๊กของโค้ด)

**วิธีแก้**
`dataset.h` ตรวจ partition เองแล้วเลือก backend:
- เจอ SPIFFS → `LittleFS`
- เจอ FAT → `FFat` (ส่ง label ของ partition ไปด้วย)
- ไม่เจอทั้งคู่ → ปิด dataset พร้อมบอกให้เปลี่ยน partition scheme

**ทดสอบ**: log ต้องขึ้น `FAT partition 'ffat' at 0x...... , xxxx KB` และ `Dataset ready: 0 samples`

---

## P3 — `Touch 0x3B: not found`  `OPEN`

**อาการ**
```
Touch 0x3B: not found (use BOOT button)
```

**สาเหตุที่เป็นไปได้**
- touch controller ไม่ได้อยู่บน I2C bus เดียวกับ TCA9554 (SDA=21/SCL=22) อาจอยู่ที่ bus ของกล้อง (8/7)
- address ไม่ใช่ `0x3B` บนบอร์ด revision นี้
- bus มี conflict กับ camera data pin GPIO21 (pin map ที่ใช้อ้างอิงมี 21 เป็น D7)

**ผลกระทบ**
ปุ่ม PREDICT บนจอไม่ทำงาน แต่ยังใช้ **BOOT กดสั้น** แทนได้ (dark frame = กดค้าง)

**สิ่งที่ต้องทำต่อ**
- สแกน I2C ทั้งสอง bus (21/22 และ 8/7) เพื่อหา `0x3B`
- ถ้าอยู่ที่ 8/7 ต้องแยก Wire instance หรือย้ายไปใช้ bus นั้นก่อน `initTouch()`

---

## P4 — ค่า wavelength calibration ยังไม่จริง  `OPEN`

**อาการ**
แกน wavelength ใช้ค่า default map เต็มคอลัมน์เป็น 380–700 nm ยังไม่ตรงกับ dispersion จริง

**วิธีแก้ที่วางไว้**
วัดหลอด fluorescent หา row ของ peak 405 nm และ 546 nm แล้วแก้
```cpp
#define CAL_PIXEL_1  <row ของ 405 nm>
#define CAL_NM_1     405.0f
#define CAL_PIXEL_2  <row ของ 546 nm>
#define CAL_NM_2     546.0f
```
ผลลัพธ์จะถูก clamp ในช่วง `SPEC_NM_MIN..SPEC_NM_MAX` (380–700)

**สำคัญ**: เปลี่ยน calibration แล้วต้องเก็บ dataset ใหม่และเทรนโมเดลใหม่

---

## P5 — โมเดล ML ยังเป็น placeholder  `OPEN`

**อาการ**
`MODEL_READY 0` → กด PREDICT ขึ้น "Model not ready"

**วิธีแก้**
เก็บ dataset ผ่านหน้าเว็บ → `python ml/train_plsr.py --csv dataset.csv --unit "<หน่วย>"` → คัดลอก `model_weights.h` → flash ใหม่
ต้องมี `MODEL_INPUT_DIM` = `CSV_POINTS` = 120 คอลัมน์

---

## P6 — `/capture` แย่ง frame กับ loop  `OPEN (ยอมรับได้)`

`handleCapture()` เรียก `esp_camera_fb_get()` แยกจาก loop ทำให้บางรอบ measurement frame ถูกใช้ไป
ผลคือค่าที่วัดอาจกระตุกเล็กน้อยตอนเปิดหน้าเว็บ; ยอมรับได้ในเวอร์ชันนี้

---

## P7 — บาง revision ต้อง init PMU (AXP2101) ก่อนใช้กล้อง  `OPEN`

เวอร์ชันหลักยังไม่ init PMU ถ้าเจอกล้องไม่ขึ้นบนบอร์ดบาง revision ให้ดูตัวอย่างใน `test/camera_server/`

---

## P8 — `Stack canary watchpoint triggered (loopTask)` ตอนมี request แรก  `FIXED`

**อาการ**
```
[ap] client connected (aid=1)
PORTAL /hotspot-detect.html
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
```
บอร์ดรีบูตทันทีที่มีคนเปิดเว็บ → หลังรีบูต AP กลับมาแต่ request ใหม่ไม่เคยถูกตอบ (ทำให้ดูเหมือน "ไม่มี request")

**สาเหตุ**
สแตกของ Arduino loop task มีขนาด default 8 KB แต่โค้ดจอง buffer ขนาดใหญ่บน stack:
- `loop()`: `float tmpR[480], tmpG[480], tmpB[480]` = 5,760 bytes
- `captureDarkFrame()`: `float[480] x3` + `double[480] x3` = ~17 KB (เกินแน่นอนเมื่อกด BOOT ค้าง)

เมื่อรวมกับ call chain ของ WebServer/camera/LCD ทำให้ canary ท้าย stack ถูกเขียนทับ

**วิธีแก้**
- ย้าย buffer ใน `loop()` เป็น `static`
- ย้าย buffer ใน `captureDarkFrame()` เป็น `static` และเปลี่ยน sum จาก `double` เป็น `float`
- เพิ่ม `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` (ภายใต้ `#ifdef` เพื่อความเข้ากันได้กับ core รุ่นต่างๆ)

**กฎที่ควรจำ**: ห้ามประกาศ array ขนาด ~1 KB ขึ้นไปบน stack ใน loop task (โดยเฉพาะ `[SPECTRUM_LEN]`)

---

## P9 — สเปกตรัมเอียง/เลื่อน และเส้นอ้างอิงไม่ตรงแนวจริง  `FIXED`

**อาการ**
1. เส้นสเปกตรัมในภาพเอียง (angular offset)
2. แนวสเปกตรัมเลื่อนซ้าย/ขวา (horizontal offset)
3. เส้นสีเหลือง (measured center column) เป็นแนวตั้งที่ x คงที่ → ไม่ทับแนวสเปกตรัมจริง
4. ค่า intensity ถูกอ่านจากตำแหน่งที่คลาดเคลื่อน

**สาเหตุ**
`extractSpectrum()` ใช้ `CENTER_COL` คงที่ทุกแถว ไม่สนใจความเอียง/การเลื่อนของแนวแสง

**วิธีแก้**
- เพิ่ม `detectCenterLine()` ใน `spectrum_processing.h`
  - หาคอลัมน์ที่สว่างที่สุดของแต่ละแถว (สุ่มทุก `CENTER_ROW_STEP = 2`)
  - ทำ 3-column box filter กัน hot pixel + refine ด้วย intensity centroid
  - fit เส้นตรง `x(y) = a*y + b` ด้วย least squares และตัด outlier 1 รอบ (2.5σ)
  - ตรวจ sanity: `|a| <= 0.8`, b อยู่ในช่วงสมเหตุสมผล, inlier >= 40 แถว
  - smooth ค่า a, b ด้วย EMA (`CENTER_EMA_ALPHA = 0.3`)
- `extractSpectrum()` เปลี่ยนไปใช้ `centerColumnForRow(y)` = `a*y+b` (clamp ขอบภาพ)
- ถ้าตรวจไม่เจอแนว (มืด/ไม่มีแสง) → fallback `CENTER_COL` และรายงาน `centerValid=false`
- LCD แสดง `a`, `b`, `conf%`
- หน้าเว็บวาด **เส้นเอียง + แถบ** ตามค่า a,b จริง (รับจาก `/data`: `slope`, `offset`, `centerValid`, `conf`)

**ปุ่ม**
- `BTN_CAL_PIN` กดสั้น → `runCenterCalibration()` เฉลี่ย 10 เฟรมแล้วล็อก a,b (แสดงผลบนจอ)
- `BTN_CAL_PIN` กดค้าง → `runWavelengthCalibration()` หา peak แรงสุดและพิมพ์ค่า nm ทาง serial
  (ยังไม่ apply — ต้องเปลี่ยน `CAL_*` เป็นตัวแปร + เก็บ NVS ก่อน)
- ปุ่มจริงยังต้องตั้ง GPIO: `BTN_MAIN_PIN` (default 0) และ `BTN_CAL_PIN` (default -1 = ปิด)

---

## ข้อมูลอ้างอิง

- Board: ESP32-S3-Touch-LCD-3.5B (ESP32S3 Dev Module, OPI PSRAM)
- Partition ที่รองรับ: `Huge APP (3MB No OTA/1MB SPIFFS)` หรือ `16M Flash (3MB APP/9.9MB FATFS)`
- AP: SSID `ITIM-project` / password `itimsarapark` / IP `192.168.4.1` / HTTP port 80
- Commit ที่เกี่ยวข้อง:
  - `a26213b` feat: add AP/dataset diagnostics and /ping endpoint
  - `b3be4d2` feat: support FFat partitions, add captive portal and mDNS
  - `3604d8b` fix: start web server before boot splash; add AP event and TLS probe logs
  - `af1a461` fix: move large spectrum buffers off the loop stack (stack canary crash)
