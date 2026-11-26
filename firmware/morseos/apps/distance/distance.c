/*
 * STRUMENTI > DISTANZA: how far the band is from the PC / phone it is connected to, estimated
 * from the link RSSI (lib/linklog: a hint, body and walls change it a lot). Shows the estimate,
 * the RSSI with 4 bars, the farthest point of this connection and the range measured at the
 * last out-of-range loss.
 *   tap        reset the farthest point
 *   hold       calibrate: "the band is 1 m from the PC now"
 *   hold 3 s   back
 */
#include "mos.h"
#include "aafont.h"
#include "linklog.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

static u32 t_draw, t_msg;

static char *put_m(char *p, u32 cm) {   /* "3.2 M", "12 M" */
    if (cm < 1000) {
        p = mos_num(p, cm / 100, 1);
        *p++ = '.';
        p = mos_num(p, cm / 10 % 10, 1);
    } else {
        p = mos_num(p, cm / 100, 1);
    }
    *p++ = ' ';
    *p++ = 'M';
    *p = 0;
    return p;
}

static char *put_dbm(char *p, int dbm) {
    *p++ = '-';
    p = mos_num(p, dbm < 0 ? -dbm : dbm, 1);
    *p++ = ' ';
    *p++ = 'D';
    *p++ = 'B';
    *p++ = 'M';
    *p = 0;
    return p;
}

static void bars(int y, int dbm) {
    int b = dbm >= -60 ? 4 : dbm >= -70 ? 3 : dbm >= -80 ? 2 : dbm >= -90 ? 1 : 0, i;
    lcd_fill(0, y, lcd.width, 12, C_BLACK);
    for (i = 0; i < 4; i++) {
        int h = 3 + 3 * i;
        lcd_fill(28 + i * 7, y + 12 - h, 5, h, i < b ? C_GREEN : C_GREY);
    }
}

static void draw_values(void) {
    char t[20], *p;
    int r = linklog_rssi(), m = linklog_min_rssi(), g = linklog_range();
    if (mos_connected() && r > -127) {
        put_m(t, linklog_distance_cm(r));
        aafont_line(&aafont_medium, 20, t, C_WHITE, C_BLACK);
        put_dbm(t, r);
        aafont_line(&aafont_small, 40, t, C_GREY, C_BLACK);
        bars(52, r);
    } else {
        aafont_line(&aafont_medium, 20, "-", C_GREY, C_BLACK);
        aafont_line(&aafont_small, 40, mos_connected() ? "..." : "NON CONNESSA", C_GREY, C_BLACK);
        lcd_fill(0, 52, lcd.width, 12, C_BLACK);
    }
    /* farthest of this connection */
    p = t;
    *p++ = 'M'; *p++ = 'A'; *p++ = 'X'; *p++ = ' ';
    if (m) put_m(p, linklog_distance_cm(m));
    else *p = 0;
    aafont_line(&aafont_medium, 72, m ? t : "MAX -", C_YELLOW, C_BLACK);
    /* range: where the link was lost the last time */
    aafont_line(&aafont_small, 94, "PERSA A", C_GREY, C_BLACK);
    if (g) {
        p = put_m(t, linklog_distance_cm(g));
        *p++ = ' ';
        put_dbm(p, g);
    }
    aafont_line(&aafont_small, 105, g ? t : "-", C_WHITE, C_BLACK);
}

static void draw(void) {
    lcd_fill(0, 0, lcd.width, 4, C_BLACK);
    aafont_line(&aafont_small, 4, "DISTANZA", C_YELLOW, C_BLACK);
    lcd_fill(0, 13, lcd.width, 7, C_BLACK);
    lcd_fill(0, 36, lcd.width, 4, C_BLACK);     /* the gaps between the lines (y: text h) */
    lcd_fill(0, 49, lcd.width, 3, C_BLACK);
    lcd_fill(0, 64, lcd.width, 8, C_BLACK);
    lcd_fill(0, 88, lcd.width, 6, C_BLACK);
    lcd_fill(0, 103, lcd.width, 2, C_BLACK);
    lcd_fill(0, 114, lcd.width, 14, C_BLACK);
    lcd_fill(0, 137, lcd.width, 2, C_BLACK);
    aafont_line(&aafont_small, 128, "TAP AZZERA MAX", C_GREY, C_BLACK);
    aafont_line(&aafont_small, 139, "TIENI QUI 1 M", C_GREY, C_BLACK);
    lcd_fill(0, 148, lcd.width, lcd.height - 148, C_BLACK);
    draw_values();
    t_draw = clock_time();
}

static void tick(void) {
    if (t_msg && mos_ms_since(t_msg) < 1200) return;   /* "1 M" message still shown */
    t_msg = 0;
    if (clock_time_exceed(t_draw, 1000 * 1000)) {
        t_draw = clock_time();
        draw_values();
    }
}

static void key(int ev) {
    if (ev == MOS_KEY_SHORT) {
        linklog_reset_min();
        draw_values();
    } else if (ev == MOS_KEY_HOLD && mos_connected() && linklog_rssi() > -127) {
        linklog_set_rssi_1m(linklog_rssi());
        aafont_line(&aafont_medium, 20, "1.0 M OK", C_GREEN, C_BLACK);
        t_msg = clock_time();
    }
}

const mos_app_t app_distance = {
    "distance", MOS_F_LONG | MOS_F_HOLD_EXIT, 120, 0, draw, tick, key, 0, 0, 0, 0, 0,
};
