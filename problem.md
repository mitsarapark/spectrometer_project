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

## P3 — `Touch 0x3B: not found`  `FIXED (รอทดสอบบนเครื่อง)`

**อาการ**
```
Touch 0x3B: not found (use BOOT button)
```

**สาเหตุจริง (ยืนยันจากตัวอย่าง Waveshare ที่ใช้ได้ 100%)**
- touch/TCA9554 อยู่บน I2C **SDA=8, SCL=7** ไม่ใช่ 21/22
- GPIO21 ที่โค้ดเดิมใช้เป็น SDA คือ **camera D7** → คำสั่ง I2C ไปชน data bus ของกล้อง
- ต้องใช้ BSP driver (`esp_lcd_touch_axs15231b.h`: `bsp_touch_init/read/get_coordinates`) ไม่ใช่ raw register 6 ไบต์

**วิธีแก้**
- `initDisplay()` เปลี่ยนเป็น `Wire.begin(8, 7)`
- `initTouch()` หลัง `esp_camera_init()`: `Wire.end()` + `Wire.begin(8, 7)` + `TCA.begin()` แล้ว
  `bsp_touch_init(&Wire, -1, 0, 320, 480)`; อ่านด้วย `bsp_touch_read()` + `bsp_touch_get_coordinates()`
- กล้องกับ touch แชร์ pad 8/7 จึงต้องเริ่ม Wire ใหม่หลังกล้อง init (กล้องไม่แตะ I2C อีกหลัง init)

---

## P3.1 — Axis calibration ถูก auto-track ดึงกลับ  `FIXED`

**อาการ**: กด `/calibrate` หรือ serial `c` แล้วค่า `a`,`b` เปลี่ยนกลับภายในไม่กี่เฟรม

**สาเหตุ**: `updateSpectrumAxis()` ทำงานทุกเฟรมและ EMA กลับไปหา fit ของตัวเอง ค่าที่ calibrate จึงถูกแทนที่

**วิธีแก้**: เพิ่ม `axisLocked` — calibration ที่สำเร็จจะ lock แนวและหยุด `updateSpectrumAxis()`
ค่า lock ถูกเก็บลง NVS (`Preferences` namespace `axis`) จึงอยู่ข้าม reboot
`AXIS_AUTO_DETECT = 0` ทำให้ไม่มี auto-tracking เขียนทับอีก; `/axis/auto` (หรือ serial `a`) = fit เฟรมเดียวแล้ว lock

---

## P3.2 — เส้นเหลืองวัดไม่ตรงแถบสเปกตรัม  `FIXED (รอทดสอบบนเครื่อง)`

**อาการ**: เส้น/แถบเหลืองบนหน้าเว็บเฉียงกว่าหรือเยื้องจากแถบสเปกตรัมจริง และเลื่อนเองตลอดเวลา

**สาเหตุ**
1. **ภาพกับเส้นมาจากคนละเฟรม** — เส้นวาดในเบราว์เซอร์จาก `/data` (เฟรมที่ประมวลผลล่าสุด) ทับภาพจาก `/capture` (เฟรมใหม่) ห่างกันได้ถึง ~1 s
2. **centroid เอนเข้าข้างที่ไม่อิ่มตัว** — `bandCenterAt()` ชั่งน้ำหนักเฉพาะพิกเซล ≥ 50% ของ peak ในหน้าต่าง ±8 px ถ้า peak อิ่มตัว (255) หลายพิกเซล น้ำหนักจะเอียงไปด้านที่มีพิกเซลไม่ clip มากกว่า
3. **สลับ order** — มี spectral order ข้างเคียงอยู่ในหน้าต่างค้นหา (±30 px) tracker จึง hopping ระหว่าง order ทำให้ slope เพี้ยน (เส้นเฉียงผิดทิศ)
4. `updateSpectrumAxis()` ทำงานทุกเฟรม (ไม่ได้รอสั่ง) ทำให้เส้นขยับเอง

**วิธีแก้**
- เปลี่ยน `bandCenterAt()` → `bandCenterNear()`: หา **half-max midpoint ของช่วงต่อเนื่องที่มี peak** (สมมาตรแม้ peak clip) และเลือก peak ที่ใกล้ตำแหน่งทำนายที่สุดถ้าความสว่าง ≥ 80% ของ peak ในหน้าต่าง
- เดินตามแถบโดยทำนายจาก **linear fit ของจุดที่เก็บได้** (ไม่ใช่แถวก่อนหน้า) + **pass 2** re-center ทุกแถวรอบเส้นที่ fit แล้ว → ตัด drift/slope เพี้ยน
- `AXIS_AUTO_DETECT = 0` → เส้นนิ่ง ขยับเฉพาะตอน calibrate และเก็บค่าใน NVS
- **ฝังเส้นเหลืองลงในภาพ `/capture` ฝั่งเฟิร์มแวร์** (`drawAxisOnFrame()`): เส้นกับสเปกตรัมมาจาก snapshot เดียวกันแล้ว; ลบ overlay canvas ในเว็บออก

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

## ข้อมูลอ้างอิง

- Board: ESP32-S3-Touch-LCD-3.5B (ESP32S3 Dev Module, OPI PSRAM)
- Partition ที่รองรับ: `Huge APP (3MB No OTA/1MB SPIFFS)` หรือ `16M Flash (3MB APP/9.9MB FATFS)`
- AP: SSID `ITIM-project` / password `itimsarapark` / IP `192.168.4.1` / HTTP port 80
- Commit ที่เกี่ยวข้อง:
  - `a26213b` feat: add AP/dataset diagnostics and /ping endpoint
  - `b3be4d2` feat: support FFat partitions, add captive portal and mDNS
  - `3604d8b` fix: start web server before boot splash; add AP event and TLS probe logs
  - `af1a461` fix: move large spectrum buffers off the loop stack (stack canary crash)
  - `b81443c` feat: auto-detect tilted spectrum axis with linear fit and calibration
  - `9687e08` fix: axis fit rejected all points because the residual threshold was scoped inside the pass loop
