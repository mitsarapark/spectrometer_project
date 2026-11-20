# PROBLEM LOG — ESP32-S3 Spectrometer

ไฟล์นี้บันทึกปัญหาที่ยังเปิดอยู่ สาเหตุ วิธีแก้ และข้อมูลที่ต้องเก็บเพิ่ม
อัปเดตล่าสุด: 2026-11-17

สถานะ: `OPEN` = ยังไม่จบ, `DONE` = ยืนยันแล้ว
รายการที่แก้แล้วให้ลบออกจากไฟล์นี้ (ประวัติยังอยู่ใน git)

---

## P1 — ค่า wavelength calibration ยังไม่จริง  `OPEN`

**อาการ**
แกน wavelength ใช้ค่า default map เต็มคอลัมน์เป็น 380–700 nm ยังไม่ตรงกับ dispersion จริง
(grating ไม่เป็นเชิงเส้นเป๊ะ จึงไม่ควรใช้แค่ 2 จุด)

**เครื่องมือที่เพิ่มแล้ว (commit `feat: runtime N-point wavelength calibration`)**
- `pixelToNm()` ใช้ตาราง runtime ได้สูงสุด `CAL_MAX_POINTS` = 8 จุด (piecewise-linear ผ่านทุกจุด,
  extrapolate จาก segment หัว/ท้าย, clamp ใน `SPEC_NM_MIN..SPEC_NM_MAX`)
- เก็บใน NVS namespace `cal` (`n`, `px0..7`, `nm0..7`) → reboot แล้วยังอยู่ ไม่ต้อง reflash
- หน้าเว็บ **Wavelength calibration**:
  - **Find peaks** → `/cal/peaks` หา peak sub-pixel จาก `spectrumCombined` (prominence + min separation)
  - กรอก nm จริงตรงช่องของแต่ละเส้น (เว้นว่างได้) → **Apply calibration** → `/cal/set`
  - **Manual** row (px) + known nm → **Add point** สำหรับเส้นที่หา peak ไม่เจอ
  - **Reset to default** → `/cal/reset` กลับไปใช้ `CAL_*`
- `/data` ส่ง `specLen`, `nmMin/Max`, `calCount`, `calActive`, `calPoints` → กราฟเว็บใช้ mapping เดียวกับ firmware
- serial `w` พิมพ์ตาราง calibration + peak ที่ตรวจได้ + ช่วง nm ของแกน
- `/spectrum/flip` และ serial `i` จะ mirror ตาราง calibration ตามลำดับ row และบันทึก axis+cal พร้อมกัน

**ทางเลือกที่ไม่ต้องเปิดเว็บ: เมนูบนจอ (ปุ่ม MENU)**
- MENU → **Lamp preset** (แตะซ้ำเพื่อวน Fluorescent/Hydrogen/Neon/Cadmium) → **Calibrate wavelength**
- matcher จับคู่ peak กับเส้นของหลอดด้วย affine RANSAC (ทุกคู่เส้น×คู่ peak) โดยยึด tolerance เป็น row
  (`LAMP_TOL_ROWS` 4) ไม่ใช่ nm → หลอดที่บีบ/ขยายภาพไม่เท่าเดิมก็ยังจับได้ แต่ pattern ที่ไม่ตรงจะถูกปฏิเสธ
- ต้องได้ ≥3 row และจุดต้องกระจาย ≥`LAMP_MIN_SPAN_NM` (50 nm); 577/579 ที่หลอมเป็น peak เดียวใช้ร่วมกันได้ (`LAMP_MERGE_NM`)
- สำเร็จแล้วบันทึกลง NVS ชุดเดียวกับหน้าเว็บ (`cal`); ล้มเหลว = ขึ้นข้อความบนจอและคงตารางเดิม
- BOOT ในเมนู: สั้น = เลื่อนรายการ, ค้าง = รันรายการที่ไฮไลต์ (serial `m` เปิด/ปิดเมนู)

**ขั้นตอนวัดจริง**
1. ตรวจลำดับ row ก่อน: `/data` ต้อง `orientKnown=true` และกราฟวิ่งน้ำเงิน (ซ้าย) → แดง (ขวา)
   ถ้าไม่ กด **Auto detect order** / **Flip spectrum**
2. โฟกัสหลอด reference ให้แถบสเปกตรัมคม (ปิด ambient light)
3. เปิดหน้าเว็บ → **Find peaks** → เทียบจำนวน/ตำแหน่ง peak กับสเปกตรัมของหลอดนั้น
4. กรอก nm ที่รู้ค่าลงช่องของ peak ที่ตรงกัน (อย่างน้อย 2 จุด; 3+ จุดดีกว่า) แล้ว **Apply calibration**
5. ตรวจซ้ำ: `/data` (`calCount`, `nmStart/End`) และ serial `w`; ลองส่องหลอดอีกชนิดเทียบตำแหน่ง peak

**ข้อจำกัด**
- จุด calibration ต้องอยู่ใน 380–700 nm และต้องเพิ่มขึ้นทั้ง px และ nm; ถ้าต้องใช้ UV (เช่น 365 nm) ต้องขยาย `SPEC_NM_MIN` ก่อน
- calibration อย่างเดียวไม่ต้องเก็บ dataset / เทรนโมเดลใหม่ เพราะ feature ยังเป็น pixel index เดิม (เปลี่ยนแค่หัวตาราง nm)
  แต่ถ้าเปลี่ยน exposure/gain/combine/axis ต้องเก็บใหม่ตามปกติ

**เกณฑ์ปิดปัญหา (acceptance)**
1. หลอด fluorescent: peak ที่ตรวจได้ตรงกับ 405/436/546/577/579 nm ภายใน tolerances ที่ยอมรับ (~1–2 nm)
2. `/data` ตอบ `calActive=true`, `calCount>=3` และ `nmStart/End` สมเหตุสมผล
3. reboot แล้วตารางเดิมยังอยู่ (log `Wavelength calibration loaded: ...`)
4. หลอดอื่น (เช่น Na หรือ Hg-Cd) peak ตรงกับค่าอ้างอิงโดยไม่ต้อง calibrate ใหม่
5. calibrate จากเมนูบนจอได้ผลเหมือนหน้าเว็บ (ไม่ต้องต่อ WiFi) และค่าอยู่หลัง reboot

---

## P2 — โมเดล ML ยังเป็น placeholder  `OPEN`

**อาการ**
`MODEL_READY 0` → กด PREDICT ขึ้น "Model not ready"

**วิธีแก้**
เก็บ dataset ผ่านหน้าเว็บ → `python ml/train_plsr.py --csv dataset.csv --unit "<หน่วย>"` → คัดลอก `model_weights.h` → flash ใหม่
ต้องมี `MODEL_INPUT_DIM` = `CSV_POINTS` = 120 คอลัมน์

---

## P3 — `/capture` แย่ง frame กับ loop  `OPEN (ยอมรับได้)`

`handleCapture()` เรียก `esp_camera_fb_get()` แยกจาก loop ทำให้บางรอบ measurement frame ถูกใช้ไป
ผลคือค่าที่วัดอาจกระตุกเล็กน้อยตอนเปิดหน้าเว็บ; ยอมรับได้ในเวอร์ชันนี้

---

## P4 — บาง revision ต้อง init PMU (AXP2101) ก่อนใช้กล้อง  `OPEN`

เวอร์ชันหลักยังไม่ init PMU ถ้าเจอกล้องไม่ขึ้นบนบอร์ดบาง revision ให้ดูตัวอย่างใน `test/camera_server/`

---

## P5 — เส้น dynamic ที่ใช้วัดสเปกตรัมยังใช้งานไม่ได้  `OPEN`

**อาการที่รายงาน**: touch ใช้ได้แล้ว แต่เส้นที่ใช้กำหนดแนววัดสเปกตรัม (เส้นเหลือง) ยังทำงานไม่ได้
ยังไม่ระบุอาการละเอียดจากเครื่องจริง — ต้องเก็บข้อมูลตามตารางด้านล่างก่อนแก้

**บริบท**: commit `b6cdeeb` เปลี่ยนเส้นให้ขยับเฉพาะตอน calibrate, เก็บ `a`,`b` ลง NVS,
ใช้ fit แบบ half-max midpoint + prediction walk และฝังเส้นลงในภาพ `/capture`
แต่ยังไม่ผ่านการทดสอบบนฮาร์ดแวร์จริง

**สิ่งที่ต้องสังเกตบนเครื่อง**

| สิ่งที่เห็น | จุดที่ต้องตรวจ |
|---|---|
| ไม่มีเส้นเหลืองในภาพ `/capture` เลย | `drawAxisOnFrame()` ถูกเรียกหรือไม่; ภาพยังเป็น RGB565 หรือถูกเปลี่ยน format |
| เห็นเส้นตรงคอลัมน์ 320 ไม่ขยับ | ยังไม่เคย calibrate สำเร็จ หรือ NVS ไม่มีค่า (บูตแล้วไม่มี log `Axis loaded from NVS`) |
| กด **Calibrate axis** แล้วขึ้น failed | `fitSpectrumAxis()` คืน false — ดู `/axis` (`detected`,`locked`), ความสว่างของแถบ, `AXIS_PEAK_FRACTION` |
| เส้นขยับแต่ยังเอียง/เยื้องจากแถบ | fit เกาะ diffraction order ผิด หรือ valley-stop ตัดผิดข้าง — เทียบค่า `a`,`b` กับภาพ |
| เส้นนิ่งแม้กด calibrate | ปุ่ม/endpoint ไม่ถึงฟังก์ชัน — ดู log `GET /calibrate` และ `Axis calib: ...` |

**ข้อมูลที่ต้องแนบเมื่อกลับมาแก้**
- ค่า `/axis` (JSON: `a`, `b`, `detected`, `locked`)
- log ตอนกด calibrate (`Axis calib: ...` หรือ `failed`)
- ภาพ `/capture` หรือภาพถ่ายจอ LCD ตอนส่องหลอดไฟ

**เกณฑ์ปิดปัญหา (acceptance)**
1. กด **Calibrate axis** → `/axis` ตอบ `locked=true` และค่า `a`,`b` ไม่เปลี่ยนเองหลังรีเฟรชหลายรอบ
2. เส้นเหลืองใน `/capture` ทับกลางแถบสเปกตรัมทุกแถว
3. reboot แล้วค่าเดิมยังอยู่ (log `Axis loaded from NVS`)

---

## P6 — Spectral response ของช่อง B อาจยังไม่แบน  `OPEN`

**อาการ**
หลังเปลี่ยน combine เป็น `(R+G+B)/3` แล้ว ปลายน้ำเงินยังอาจอ่อนกว่าที่ตาเห็น เพราะช่อง B ของ sensor
ตอบสนองต่ำกว่า G และ RGB565 ให้ B แค่ 5 bit (G 6 bit) — ค่า default ยังไม่ชดเชยส่วนนี้

**วิธีแก้/ตรวจ**
1. วัด broadband white reference (หลอด incandescent/halogen หรือ white LED ที่ spectrum ต่อเนื่อง)
2. เทียบสเปกตรัมที่ได้กับ reference; ถ้าปลายน้ำเงินต่ำกว่าอย่างเป็นระบบ ให้ตั้ง `COMBINE_GAIN_B` (และ/หรือ R) ใน `spectrometer.ino`
3. เก็บ dataset ใหม่และเทรนโมเดลใหม่หลังแก้ gain

**หมายเหตุ**: `COMBINE_GAIN_*` เป็นการชดเชยแบบง่าย (flat-field ต่อ channel) ไม่ใช่ calibration เชิงสเปกตรัมเต็มรูปแบบ
ถ้าต้องการความแม่นยำเชิงปริมาณ ต้องใช้ reference lamp + responsivity curve

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
  - `b6cdeeb` fix: manual-only axis calibration with NVS; align yellow line with band
