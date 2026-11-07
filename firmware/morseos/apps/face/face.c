/*
 * Watch face (home): big 7-segment 00:00, weekday + day, BT and battery on top, the Morse
 * being typed below. Typing the access word opens the menu.
 */
#include "mos.h"
#include "battery.h"
#include "font.h"
#include "rtc.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

static const char *const wday_it[7] = {"DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB"};
static int last_min = -1, last_level = -1, last_charging = -1, last_conn = -1;

/* 7-segment digit, 14 x 29 px, segment thickness 3 */
static void digit7(int x, int y, int d, u16 fg) {
    static const u8 seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const int w = 14, t = 3, v = 10;
    u8 m = seg[d % 10];
    lcd_fill(x, y, w, 3 * t + 2 * v, C_BLACK);
    if (m & 0x01) lcd_fill(x + t, y, w - 2 * t, t, fg);                     /* a */
    if (m & 0x02) lcd_fill(x + w - t, y + t, t, v, fg);                     /* b */
    if (m & 0x04) lcd_fill(x + w - t, y + 2 * t + v, t, v, fg);             /* c */
    if (m & 0x08) lcd_fill(x + t, y + 2 * t + 2 * v, w - 2 * t, t, fg);     /* d */
    if (m & 0x10) lcd_fill(x, y + 2 * t + v, t, v, fg);                     /* e */
    if (m & 0x20) lcd_fill(x, y + t, t, v, fg);                             /* f */
    if (m & 0x40) lcd_fill(x + t, y + t + v, w - 2 * t, t, fg);             /* g */
}

static void draw_status(void) {
    int lvl = battery_level(), chg = battery_charging(), w;
    u16 col = chg ? C_BLUE : lvl >= 50 ? C_GREEN : lvl >= 25 ? C_YELLOW : C_RED;
    lcd_fill(0, 0, lcd.width, 12, C_BLACK);
    font_text(3, 2, "BT", 1, mos_connected() ? C_GREEN : C_GREY, C_BLACK);
    lcd_fill(56, 2, 18, 1, C_WHITE);   /* battery outline + nub */
    lcd_fill(56, 10, 18, 1, C_WHITE);
    lcd_fill(56, 2, 1, 9, C_WHITE);
    lcd_fill(73, 2, 1, 9, C_WHITE);
    lcd_fill(74, 4, 2, 5, C_WHITE);
    w = chg ? 14 : lvl * 14 / 100;
    if (w < 1) w = 1;
    lcd_fill(58, 4, w, 5, col);
    last_level = lvl;
    last_charging = chg;
    last_conn = mos_connected();
}

static void draw_time(void) {
    rtc_time_t t;
    char d[12], *p = d;
    const char *w;
    const int y = 40, x = 5;
    u16 col = rtc_is_set() ? C_WHITE : C_GREY;
    rtc_get(&t);
    digit7(x, y, t.hour / 10, col);
    digit7(x + 17, y, t.hour % 10, col);
    lcd_fill(x + 33, y + 8, 3, 3, col);   /* colon */
    lcd_fill(x + 33, y + 18, 3, 3, col);
    digit7(x + 39, y, t.min / 10, col);
    digit7(x + 56, y, t.min % 10, col);
    for (w = rtc_is_set() ? wday_it[t.wday] : "---"; *w;) *p++ = *w++;
    *p++ = ' ';
    mos_num(p, rtc_is_set() ? t.day : 0, 1);
    lcd_fill(0, 82, lcd.width, 16, C_BLACK);
    mos_center_text(82, d, 2, C_WHITE);
    last_min = t.min;
}

static void draw(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    draw_status();
    draw_time();
}

static void tick(void) {
    rtc_time_t t;
    rtc_get(&t);
    if (t.min != last_min) draw_time();
    if (battery_level() != last_level || battery_charging() != last_charging ||
        mos_connected() != last_conn)
        draw_status();
}

static void pattern(const char *pat) { mos_pattern_at(128, pat, C_YELLOW, 0); }

static void word(const char *w) {
    lcd_fill(0, 110, lcd.width, 8, C_BLACK);
    mos_center_text(110, w, 1, C_GREY);
}

static void access(void) {
    mos_buzz(60);
    sleep_us(80 * 1000);
    mos_buzz(60);
    mos_menu("MENU");
}

const mos_app_t app_face = {
    "face", MOS_F_HOME | MOS_F_MORSE, 15, 0, draw, tick, 0, 0, pattern, word, access,
};
