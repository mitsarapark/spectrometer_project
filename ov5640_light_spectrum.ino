#include "esp_camera.h"

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 38
#define SIOD_GPIO_NUM  8
#define SIOC_GPIO_NUM  7
#define Y9_GPIO_NUM 21
#define Y8_GPIO_NUM 39
#define Y7_GPIO_NUM 40
#define Y6_GPIO_NU 42
#define Y5_GPIO_NUM 46
#define Y4_GPIO_NUM 48
#define Y3_GPIO_NUM 47
#define Y2_GPIO_NUM 45
#define VSYNC_GPIO_NUM 17
#define HREF_GPIO_NUM 18
#define PCLK_GPIO_NUM 41

const int fram_w = 320;    
const int fram_h = 240;
const int scan_row = fram_h / 2;   
const int STEP = 2;               

// not true depend on light bull that we need to calibrate
const int CAL_PX1 = 60,  CAL_WL1 = 450;  
const int CAL_PX2 = 260, CAL_WL2 = 650;  

float slope = (float)(CAL_WL2 - CAL_WL1) / (float)(CAL_PX2 - CAL_PX1);

float pixelToWavelength(int px) {
  return CAL_WL1 + slope * (px - CAL_PX1);
}

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;   
  config.frame_size = FRAMESIZE_QVGA;    
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_whitebal(s, 0);
    s->set_awb_gain(s, 0);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!initCamera()) {
    Serial.println("Camera failed");
    while (true) delay(1000);
  }

  Serial.println("Camera ready");
  Serial.println("px,wavelength_nm,R,G,B,intensity");
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Frame capture failed");
    delay(200);
    return;
  }

  if (fb->format == PIXFORMAT_RGB565 && fb->width == fram_w && fb->height == fram_h) {
    uint16_t *pixels = (uint16_t *)fb->buf;

    for (int x = 0; x < fram_w; x += STEP) {
      uint16_t px = pixels[scan_row * fram_w + x];
      uint8_t r = ((px >> 11) & 0x1F) << 3;
      uint8_t g = ((px >> 5)  & 0x3F) << 2;
      uint8_t b = (px & 0x1F) << 3;
      uint16_t intensity = (uint16_t)r + g + b;

      float wl = pixelToWavelength(x);

      Serial.print(x);          Serial.print(',');
      Serial.print(wl, 1);      Serial.print(',');
      Serial.print(r);          Serial.print(',');
      Serial.print(g);          Serial.pr*int(',');
      Serial.print(b);          Serial.print(',');
      Serial.println(intensity);
    }
    Serial.println(); 
  } else {
    Serial.println("Unexpected frame format/size");
  }

  esp_camera_fb_return(fb);
  delay(300); 
}
