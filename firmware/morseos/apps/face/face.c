/*
 * Watch face (home): big 00:00 with the colon blinking (1 s on, 1 s off), weekday + day, BT and
 * battery on top, the Morse being typed below. Typing the access word opens the menu.
 * The clock is chosen at build time, [options] clock in bundle.ini:
 *   font      antialiased Ubuntu Condensed Bold digits (clock_font.h, run-length coded)
 *   segments  the 7-segment digits of MorseOS 0.3-0.6
 * Holding the key past the reset threshold clears the input: the text line turns red and a
 * wiper sweeps it away from right to left (mos_wipe).
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
static int last_sec = -1, colon_on = 1;

#ifdef MOS_OPT_CLOCK_SEGMENTS
/* 7-segment: digits 14 x 29 at x 5, 22, 44, 61, colon at x 38 */
#define TIME_Y  40
#define TIME_H  29
#define COLON_X 38
#define COLON_W 3
static const u8 digit_x[4] = {5, 22, 44, 61};
#else
#include "clock_font.h"
/* font: 19 px cells at x 0, 19, 42, 61, colon in the 4 px between them */
#define TIME_Y  37
#define TIME_H  CLOCK_H
#define COLON_X 38
#define COLON_W 4
static const u8 digit_x[4] = {0, 19, 42, 61};
#endif

#ifdef MOS_OPT_CLOCK_SEGMENTS
/* 7-segment digit, 14 x 29 px, segment thickness 3 */
static void digit(int x, int y, int d, u16 fg) {
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

static void colon(u16 fg) {
    lcd_fill(COLON_X, TIME_Y, COLON_W, TIME_H, C_BLACK);
    lcd_fill(COLON_X, TIME_Y + 8, 3, 3, fg);
    lcd_fill(COLON_X, TIME_Y + 18, 3, 3, fg);
}
#else
static void digit(int x, int y, int d, u16 fg) {
    lcd_blit_rle(x, y, CLOCK_W, CLOCK_H, CLOCK_DIGIT(d % 10), fg, C_BLACK);
}

/* two 4 x 4 dots with rounded corners, at the height of a colon in the font */
static void colon(u16 fg) {
    static const u8 dy[2] = {CLOCK_H * 3 / 10, CLOCK_H * 7 / 10 - 4};
    int i;
    lcd_fill(COLON_X, TIME_Y, COLON_W, TIME_H, C_BLACK);
    for (i = 0; i < 2; i++) {
        lcd_fill(COLON_X + 1, TIME_Y + dy[i], 2, 4, fg);
        lcd_fill(COLON_X, TIME_Y + dy[i] + 1, 4, 2, fg);
    }
}
#endif

static u16 time_color(void) { return rtc_is_set() ? C_WHITE : C_GREY; }

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

/* inside of the battery icon: w of 14 px in col, the rest black */
static void draw_fill(int w, u16 col) {
    if (w < 1) w = 1;
    if (w > 14) w = 14;
    lcd_fill(58, 4, w, 5, col);
    if (w < 14) lcd_fill(58 + w, 4, 14 - w, 5, C_BLACK);
}

static int pump;          /* charging animation: extra px above the level, 0..14 */
static u32 t_pump, t_sense;

static void draw_battery(void) {
    int lvl = battery_level(), chg = battery_charging(), done = battery_full();
    u16 col = chg && !done ? C_BLUE : lvl >= 50 ? C_GREEN : lvl >= 25 ? C_YELLOW : C_RED;
    lcd_fill(54, 0, lcd.width - 54, 12, C_BLACK);
    lcd_fill(56, 2, 18, 1, C_WHITE);   /* battery outline + nub */
    lcd_fill(56, 10, 18, 1, C_WHITE);
    lcd_fill(56, 2, 1, 9, C_WHITE);
    lcd_fill(73, 2, 1, 9, C_WHITE);
    lcd_fill(74, 4, 2, 5, C_WHITE);
    pump = 0;
    t_pump = clock_time();
    draw_fill(lvl * 14 / 100, col);
    last_level = lvl * 14 / 100;       /* redraw only when the bar would change */
    last_charging = chg | done << 1;
}

/* charging: the fill climbs from the level to full in 4 steps, then starts again; near full
 * it still climbs from 10 px so the animation is always visible until the charge is done */
static void pump_battery(void) {
    int base = battery_level() * 14 / 100, step;
    if (base > 10) base = 10;
    step = (14 - base + 3) / 4;
    if (!clock_time_exceed(t_pump, 400 * 1000)) return;
    t_pump = clock_time();
    if (step < 1) step = 1;
    pump = base + pump >= 14 ? 0 : pump + step;
    draw_fill(base + pump, C_BLUE);
}

static void draw_time(void) {
    rtc_time_t t;
    char d[12], *p = d;
    const char *w;
    u16 col = time_color();
    rtc_get(&t);
    digit(digit_x[0], TIME_Y, t.hour / 10, col);
    digit(digit_x[1], TIME_Y, t.hour % 10, col);
    colon(colon_on ? col : C_BLACK);
    digit(digit_x[2], TIME_Y, t.min / 10, col);
    digit(digit_x[3], TIME_Y, t.min % 10, col);
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
    lcd_fill(0, 12, lcd.width, TIME_Y - 12, C_BLACK);
#ifdef MOS_OPT_CLOCK_SEGMENTS                  /* the gaps around the digits */
    lcd_fill(0, TIME_Y, 5, TIME_H, C_BLACK);
    lcd_fill(19, TIME_Y, 3, TIME_H, C_BLACK);
    lcd_fill(36, TIME_Y, 2, TIME_H, C_BLACK);
    lcd_fill(41, TIME_Y, 3, TIME_H, C_BLACK);
    lcd_fill(58, TIME_Y, 3, TIME_H, C_BLACK);
    lcd_fill(75, TIME_Y, lcd.width - 75, TIME_H, C_BLACK);
#endif
    lcd_fill(0, TIME_Y + TIME_H, lcd.width, 82 - TIME_Y - TIME_H, C_BLACK);
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
    /* colon: on for a whole second, off for the next (0.8.3: was half a second each, too
     * fast); left on in the dark so the face wakes up complete */
    if (t.sec != last_sec) {
        int on = !(t.sec & 1) || !mos_screen_on();
        last_sec = t.sec;
        if (on != colon_on) colon(on ? time_color() : C_BLACK);
        colon_on = on;
    }
    /* screen on: read the charger every 2 s, so plugging in shows at once (battery_poll in the
     * main loop only samples every 30 s) */
    if (mos_screen_on() && clock_time_exceed(t_sense, 2 * 1000 * 1000)) {
        t_sense = clock_time();
        battery_update();
    }
    if (battery_level() * 14 / 100 != last_level ||
        (battery_charging() | battery_full() << 1) != last_charging)
        draw_battery();
    else if (last_charging == 1 && mos_screen_on()) pump_battery();
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

/* long press: input cleared, shown by the wiper on the text line; typing starts again after
 * the release (the wiper finishes by itself) */
static void reset(int held) {
    if (held) mos_wipe(104, 16);
    mos_pattern_at(126, "", C_YELLOW, 1);
}

static void access(void) {
    mos_buzz(60);
    sleep_us(80 * 1000);
    mos_buzz(60);
    mos_menu("MENU");
}

const mos_app_t app_face = {
    "face", MOS_F_HOME | MOS_F_MORSE, 15, 0, draw, tick, 0, 0, pattern, word, access, reset,
};
