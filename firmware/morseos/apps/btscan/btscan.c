/*
 * STRUMENTI > BLUETOOTH: the devices advertising nearby, strongest first: name (or the end of
 * the address) and signal. Scans only while it is open (lib/blescan). Tap: clear the list.
 * Hold 3 s: back.
 */
#include "mos.h"
#include "blescan.h"
#include "font.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define ROW_Y0 16
#define ROW_H  14
#define ROWS   BLESCAN_MAX

static u32 t_draw;

static char hex(int v) { return "0123456789ABCDEF"[v & 15]; }

static void draw_row(int r, const blescan_dev_t *d) {
    int y = ROW_Y0 + r * ROW_H, bars, i;
    char t[14], *p = t;
    lcd_fill(0, y, lcd.width, ROW_H, C_BLACK);
    if (!d) return;
    if (d->name[0]) {
        const char *n = d->name;
        while (*n) *p++ = *n++;
    } else {                               /* no name: the last 3 bytes of the address */
        for (i = 2; i >= 0; i--) {
            *p++ = hex(d->mac[i] >> 4);
            *p++ = hex(d->mac[i]);
            if (i) *p++ = ':';
        }
    }
    *p = 0;
    font_text(2, y + 3, t, 1, blescan_age_s(d) > 10 ? C_GREY : C_WHITE, C_BLACK);
    /* signal: dBm as 4 bars, like the watch face */
    bars = d->rssi >= -60 ? 4 : d->rssi >= -70 ? 3 : d->rssi >= -80 ? 2 : d->rssi >= -90 ? 1 : 0;
    for (i = 0; i < 4; i++) {
        int h = 2 + 2 * i;
        lcd_fill(62 + i * 4, y + 11 - h, 3, h, i < bars ? C_GREEN : C_GREY);
    }
}

static void draw_list(void) {
    u8 idx[BLESCAN_MAX];
    int n = blescan_sorted(idx), r;
    char t[8], *p;
    for (r = 0; r < ROWS; r++) draw_row(r, r < n ? blescan_dev(idx[r]) : 0);
    p = mos_num(t, n, 1);
    *p = 0;
    lcd_fill(60, 3, lcd.width - 60, 8, C_BLACK);
    font_text(lcd.width - 2 - font_width(t, 1), 3, t, 1, C_YELLOW, C_BLACK);
}

static void enter(void) { blescan_request(1500); }

static void draw(void) {
    lcd_fill(0, 0, lcd.width, ROW_Y0, C_BLACK);
    font_text(2, 3, "BLUETOOTH", 1, C_YELLOW, C_BLACK);
    lcd_fill(0, ROW_Y0 + ROWS * ROW_H, lcd.width, lcd.height - ROW_Y0 - ROWS * ROW_H, C_BLACK);
    mos_center_text(138, "TAP: AZZERA", 1, C_GREY);
    mos_center_text(148, "TIENI 3S ESCI", 1, C_GREY);
    draw_list();
    t_draw = clock_time();
}

/* keep the scan alive while shown (it stops by itself 1.5 s after leaving), list every 1 s */
static void tick(void) {
    blescan_request(1500);
    if (clock_time_exceed(t_draw, 1000 * 1000)) {
        t_draw = clock_time();
        draw_list();
    }
}

static void key(int ev) {
    blescan_clear();
    draw_list();
}

const mos_app_t app_btscan = {
    "btscan", MOS_F_HOLD_EXIT, 60, enter, draw, tick, key, 0, 0, 0, 0, 0,
};
