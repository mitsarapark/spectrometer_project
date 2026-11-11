#ifndef TOUCH_H
#define TOUCH_H

/*
 * AXS15231B integrated touch controller (Waveshare BSP driver).
 *
 * The panel is wired to the camera SCCB pads (SDA=8, SCL=7) on this
 * board, not to 21/22 - GPIO21 is camera D7, which is why the controller
 * never answered on the old bus.
 *
 * initDisplay() already starts Wire on 8/7 for the TCA9554, but
 * esp_camera_init() then claims those pads for its own I2C port. The
 * driver is therefore restarted here before the panel is opened;
 * afterwards the camera is never touched on I2C again (all its registers
 * are written during init), so the two can share the bus.
 *
 * If the controller does not answer at TOUCH_I2C_ADDR the UI silently
 * falls back to the BOOT button, so a wrong board revision cannot brick
 * the build.
 */

#include "esp_lcd_touch_axs15231b.h"

bool touchAvailable = false;

bool initTouch() {
#if TOUCH_ENABLE
    // Re-own the bus after the camera driver reconfigured its pads.
    Wire.end();
    Wire.begin(I2C_SDA_GPIO, I2C_SCL_GPIO);
    TCA.begin();

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    touchAvailable = (Wire.endTransmission() == 0);
    Serial.printf("Touch 0x%02X: %s\n", TOUCH_I2C_ADDR,
                  touchAvailable ? "found" : "not found (use BOOT button)");

    if (touchAvailable) {
        bsp_touch_init(&Wire, -1, 0, DISP_WIDTH, DISP_HEIGHT);
    }
#else
    touchAvailable = false;
    Serial.println("Touch disabled by config");
#endif
    return touchAvailable;
}

// Reads the panel through the BSP: bsp_touch_read() refreshes the report,
// bsp_touch_get_coordinates() returns false when no finger is down.
bool readTouch(uint16_t &x, uint16_t &y) {
    if (!touchAvailable) return false;

    touch_data_t td;
    bsp_touch_read();
    if (!bsp_touch_get_coordinates(&td)) return false;

    x = td.coords[0].x;
    y = td.coords[0].y;

    if (x >= DISP_WIDTH)  x = DISP_WIDTH  - 1;
    if (y >= DISP_HEIGHT) y = DISP_HEIGHT - 1;
    return true;
}

#endif // TOUCH_H
