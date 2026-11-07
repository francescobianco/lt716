/*
 * SENSORI: find the I2C devices on the unplaced pins (lib/i2cprobe) and show them:
 * "A3>A4 44:21" = data on PA3, clock on PA4, address 0x44, ID 0x21, then the chip name.
 * Short touch: scan again. Hold 3 s: back.
 */
#include "mos.h"
#include "font.h"
#include "i2cprobe.h"
#include "st7735.h"

static i2cprobe_result_t res;

static char hex(int v) { return "0123456789ABCDEF"[v & 15]; }

static void scan(void) { i2cprobe_run(&res); }

static void draw(void) {
    int i, y = 18;
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    mos_center_text(3, "SENSORI", 1, C_YELLOW);
    if (!res.n) mos_center_text(40, "NESSUNO", 1, C_GREY);
    for (i = 0; i < res.n && y < lcd.height - 30; i++) {
        const i2cprobe_hit_t *h = &res.hit[i];
        char t[16];
        const char *a = i2cprobe_pin_name(h->sda), *b = i2cprobe_pin_name(h->scl);
        t[0] = a[0]; t[1] = a[1]; t[2] = '>'; t[3] = b[0]; t[4] = b[1]; t[5] = ' ';
        t[6] = hex(h->addr >> 4); t[7] = hex(h->addr);
        if (h->id_reg != 0xFF) {
            t[8] = ':'; t[9] = hex(h->id >> 4); t[10] = hex(h->id); t[11] = 0;
        } else {
            t[8] = 0;
        }
        font_text(2, y, t, 1, C_WHITE, C_BLACK);
        font_text(14, y + 9, i2cprobe_guess(h), 1, C_GREEN, C_BLACK);
        y += 20;
    }
    if (res.busy_mask) {
        char t[14] = "BASSI:", *p = t + 6;
        for (i = 0; i < 4; i++)
            if (res.busy_mask >> i & 1) {
                const char *n = i2cprobe_pin_name(i);
                *p++ = n[0];
                *p++ = n[1];
            }
        *p = 0;
        font_text(2, lcd.height - 28, t, 1, C_GREY, C_BLACK);
    }
    mos_center_text(lcd.height - 18, "TOCCA: RIPETI", 1, C_GREY);
}

static void key(int ev) {
    scan();
    draw();
}

const mos_app_t app_sensors = {
    "sensors", MOS_F_HOLD_EXIT, 60, scan, draw, 0, key, 0, 0, 0, 0,
};
