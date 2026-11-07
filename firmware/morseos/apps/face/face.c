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
static int last_min = -1, last_level = -1, last_charging = -1, last_conn = -1, last_bars = -1;
static char last_peer[4];

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

static void draw_link(void) {
    const char *who = mos_peer();
    int bars = mos_signal_bars(), i;
    lcd_fill(0, 0, 54, 12, C_BLACK);
    if (mos_connected()) {
        /* who is connected (first 3 letters of its name) and the signal, like a phone */
        int x = 3;
        if (who[0]) x = font_text(3, 2, who, 1, C_WHITE, C_BLACK) + 2;  /* name, if it told us */
        for (i = 0; i < 4; i++) {
            int h = 2 + 2 * i;
            lcd_fill(x + i * 4, 10 - h, 3, h, i < bars ? C_WHITE : C_GREY);
        }
    }
    last_conn = mos_connected();
    last_bars = bars;
    for (i = 0; i < 3 && who[i]; i++) last_peer[i] = who[i];
    last_peer[i] = 0;
}

static int link_changed(void) {
    const char *who = mos_peer();
    int i;
    if (mos_connected() != last_conn || mos_signal_bars() != last_bars) return 1;
    for (i = 0; i < 4; i++) {
        if (who[i] != last_peer[i]) return 1;
        if (!who[i]) break;
    }
    return 0;
}

static void draw_battery(void) {
    int lvl = battery_level(), chg = battery_charging(), w;
    u16 col = chg ? C_BLUE : lvl >= 50 ? C_GREEN : lvl >= 25 ? C_YELLOW : C_RED;
    lcd_fill(54, 0, lcd.width - 54, 12, C_BLACK);
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

static void word(const char *w);

/* full draw, band by band, each pixel written once: no black flash */
static void draw(void) {
    draw_link();
    draw_battery();
    lcd_fill(0, 12, lcd.width, 28, C_BLACK);
    lcd_fill(0, 40, lcd.width, 30, C_BLACK);
    lcd_fill(0, 70, lcd.width, 12, C_BLACK);
    draw_time();                               /* digits and the date line (82..97) */
    lcd_fill(0, 98, lcd.width, 6, C_BLACK);
    word("");                                  /* 104..119 */
    lcd_fill(0, 120, lcd.width, 6, C_BLACK);
    mos_pattern_at(126, "", C_YELLOW, 1);      /* 126..133 */
    lcd_fill(0, 134, lcd.width, lcd.height - 134, C_BLACK);
}

static void tick(void) {
    rtc_time_t t;
    rtc_get(&t);
    if (t.min != last_min) draw_time();
    if (battery_level() != last_level || battery_charging() != last_charging) draw_battery();
    if (link_changed()) draw_link();
}

static void pattern(const char *pat) { mos_pattern_at(126, pat, C_YELLOW, 1); }

/* letters typed so far: big and white, the last 6 that fit */
static void word(const char *w) {
    int n = 0;
    while (w[n]) n++;
    if (n > 6) w += n - 6;
    lcd_fill(0, 104, lcd.width, 16, C_BLACK);
    mos_center_text(104, w, 2, C_WHITE);
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
