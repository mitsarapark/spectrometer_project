#ifndef TOUCH_H
#define TOUCH_H

/*
 * AXS15231B integrated touch controller.
 *
 * The panel shares the Wire bus that is already initialised for the
 * TCA9554 IO expander (pins set in display.h / initDisplay()). If the
 * controller does not answer at TOUCH_I2C_ADDR the UI silently falls
 * back to the BOOT button, so a wrong board revision cannot brick the
 * build.
 *
 * Protocol (6-payload-byte report, same as Waveshare's demo):
 *   cmd 0x00 -> [status, points, x_hi, x_lo, y_hi, y_lo, ...]
 */

bool touchAvailable = false;

bool initTouch() {
#if TOUCH_ENABLE
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    touchAvailable = (Wire.endTransmission() == 0);
    Serial.printf("Touch 0x%02X: %s\n", TOUCH_I2C_ADDR,
                  touchAvailable ? "found" : "not found (use BOOT button)");
#else
    touchAvailable = false;
    Serial.println("Touch disabled by config");
#endif
    return touchAvailable;
}

bool readTouch(uint16_t &x, uint16_t &y) {
    if (!touchAvailable) return false;

    uint8_t buf[8] = {0};
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)8) != 8) return false;
    for (int i = 0; i < 8; i++) buf[i] = Wire.read();

    if ((buf[1] & 0x0F) == 0) return false; // no finger down

    x = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    y = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];

    if (x >= DISP_WIDTH)  x = DISP_WIDTH  - 1;
    if (y >= DISP_HEIGHT) y = DISP_HEIGHT - 1;
    return true;
}

#endif // TOUCH_H
