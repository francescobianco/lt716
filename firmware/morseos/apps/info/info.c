/* INFO: version, battery, Bluetooth, uptime, the learned Morse timing. Any touch: back. */
#include "mos.h"
#include "battery.h"
#include "font.h"
#include "morse.h"
#include "rtc.h"
#include "st7735.h"

static void line(int row, const char *a, u32 v, const char *b) {
    char t[24], *p = t;
    while (*a) *p++ = *a++;
    if (b) {
        p = mos_num(p, v, 1);
        while (*b) *p++ = *b++;
    }
    *p = 0;
    font_text(3, 4 + row * 12, t, 1, C_WHITE, C_BLACK);
}

static void draw(void) {
    u32 up = rtc_uptime();
    battery_update();
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    line(0, "MORSEOS " MOS_VERSION, 0, 0);
    line(2, "BAT ", battery_mv(), "MV");
    line(3, battery_charging() ? "CARICA " : "LIV ", battery_level(), "%");
    line(4, mos_connected() ? "BT ON" : "BT OFF", 0, 0);
    line(5, "ACCESO ", up / 3600, "H");
    line(6, "       ", up / 60 % 60, "M");
    line(8, "PUNTO ", morse_dot_ms(), "MS");
    line(9, "LINEA ", morse_dash_ms(), "MS");
    line(11, rtc_is_set() ? "ORA OK" : "ORA DA FARE", 0, 0);
}

static void key(int ev) { mos_back(); }

const mos_app_t app_info = {"info", 0, 30, 0, draw, 0, key, 0, 0, 0, 0};
