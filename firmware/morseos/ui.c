/*
 * MorseOS UI on the 80x160 portrait TFT, key at the bottom.
 *
 *   FACE   big 00:00, weekday + day, BT and battery on top, touch mark at the bottom.
 *          The touch key is read as Morse; the access word opens the menu.
 *   MENU   two square boxes per screen, the top one is selected. Short touch: the bottom
 *          box moves up and the next item appears below (circular). Long touch: open.
 *   INFO   device information; any touch goes back to the menu.
 *   MSG    a notification; any touch goes back to the face.
 * The screen turns off after a while without touches; the touch that wakes it only
 * shows the face. A touch mark at the bottom is always drawn while the key is down.
 */
#include "ui.h"
#include "battery.h"
#include "font.h"
#include "morse.h"
#include "rtc.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define FACE_TIMEOUT_MS  15000
#define OTHER_TIMEOUT_MS 30000
#define MSG_TIMEOUT_MS   10000
#define LONG_MS          600
#define BATTERY_EVERY_S  30

enum { S_FACE, S_MENU, S_INFO, S_MSG };
enum { M_INFO, M_BACK, M_COUNT };
static const char *const menu_label[M_COUNT] = {"INFO", "TORNA"};
static const char *const wday_it[7] = {"DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB"};

static const ui_cb_t *cb;
static int screen = S_FACE, screen_on = 1;
static int menu_top;            /* index of the item in the top box */
static int pressed, consumed, long_fired;
static u32 t_press, t_activity;
static int connected;
static int last_min = -1, last_level = -1, last_charging = -1, last_conn = -1;
static u32 last_battery_s;
static char msg_text[200];

static u32 ms_since(u32 t) { return (clock_time() - t) / (sys_tick_per_us * 1000); }

/* ------------------------------------------------------------------ drawing */

static char *num(char *p, u32 v, int width) {
    char t[10];
    int n = 0;
    do t[n++] = '0' + v % 10, v /= 10; while (v);
    while (n < width) t[n++] = '0';
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void center_text(int y, const char *s, int scale, u16 fg) {
    font_text((lcd.width - font_width(s, scale)) / 2, y, s, scale, fg, C_BLACK);
}

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

static void draw_touch_mark(int on) {
    if (!screen_on) return;
    lcd_fill(lcd.width / 2 - 4, lcd.height - 7, 8, 5, on ? C_YELLOW : C_GREY);
}

static void draw_status(void) {
    int lvl = battery_level(), chg = battery_charging(), w;
    u16 col = chg ? C_BLUE : lvl >= 50 ? C_GREEN : lvl >= 25 ? C_YELLOW : C_RED;
    lcd_fill(0, 0, lcd.width, 12, C_BLACK);
    font_text(3, 2, "BT", 1, connected ? C_GREEN : C_GREY, C_BLACK);
    /* battery: 18x9 outline + nub */
    lcd_fill(56, 2, 18, 1, C_WHITE);
    lcd_fill(56, 10, 18, 1, C_WHITE);
    lcd_fill(56, 2, 1, 9, C_WHITE);
    lcd_fill(73, 2, 1, 9, C_WHITE);
    lcd_fill(74, 4, 2, 5, C_WHITE);
    w = lvl * 14 / 100;
    if (w < 1 && !chg) w = 1;
    if (chg) w = 14;
    lcd_fill(58, 4, w, 5, col);
    last_level = lvl;
    last_charging = chg;
    last_conn = connected;
}

static void draw_time(void) {
    rtc_time_t t;
    char d[12], *p;
    const int y = 40, x = 5;
    u16 col = rtc_is_set() ? C_WHITE : C_GREY;
    rtc_get(&t);
    digit7(x, y, t.hour / 10, col);
    digit7(x + 17, y, t.hour % 10, col);
    lcd_fill(x + 33, y + 8, 3, 3, col);   /* colon */
    lcd_fill(x + 33, y + 18, 3, 3, col);
    digit7(x + 39, y, t.min / 10, col);
    digit7(x + 56, y, t.min % 10, col);
    p = d;
    {
        const char *w = rtc_is_set() ? wday_it[t.wday] : "---";
        while (*w) *p++ = *w++;
    }
    *p++ = ' ';
    num(p, rtc_is_set() ? t.day : 0, 1);
    lcd_fill(0, 82, lcd.width, 16, C_BLACK);
    center_text(82, d, 2, C_WHITE);
    last_min = t.min;
}

static void draw_pattern(const char *pat) {
    int x = 4;
    if (screen != S_FACE || !screen_on) return;
    lcd_fill(0, 128, lcd.width, 6, C_BLACK);
    for (; *pat; pat++) {
        int w = *pat == '.' ? 4 : 12;
        lcd_fill(x, 129, w, 4, C_YELLOW);
        x += w + 3;
    }
}

static void draw_word(const char *word) {
    if (screen != S_FACE || !screen_on) return;
    lcd_fill(0, 110, lcd.width, 8, C_BLACK);
    center_text(110, word, 1, C_GREY);
}

static void draw_face(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    draw_status();
    draw_time();
    draw_touch_mark(pressed);
}

static void draw_box(int y, const char *label, int selected) {
    u16 c = selected ? C_WHITE : C_GREY;
    int x = 4, w = 72, h = 66, t = selected ? 2 : 1;
    lcd_fill(x, y, w, h, C_BLACK);
    lcd_fill(x, y, w, t, c);
    lcd_fill(x, y + h - t, w, t, c);
    lcd_fill(x, y, t, h, c);
    lcd_fill(x + w - t, y, t, h, c);
    font_text(x + (w - font_width(label, 2)) / 2, y + (h - 16) / 2, label, 2, c, C_BLACK);
}

static void draw_menu(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    draw_box(8, menu_label[menu_top], 1);
    draw_box(80, menu_label[(menu_top + 1) % M_COUNT], 0);
    draw_touch_mark(pressed);
}

static void info_line(int row, const char *a, u32 v, const char *b) {
    char t[24], *p = t;
    while (*a) *p++ = *a++;
    if (b) {
        p = num(p, v, 1);
        while (*b) *p++ = *b++;
    }
    *p = 0;
    font_text(3, 4 + row * 12, t, 1, C_WHITE, C_BLACK);
}

static void draw_info(void) {
    u32 up = rtc_uptime();
    battery_update();
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    info_line(0, "MORSEOS " MORSEOS_VERSION, 0, 0);
    info_line(2, "BAT ", battery_mv(), "MV");
    info_line(3, battery_charging() ? "CARICA " : "LIV ", battery_level(), "%");
    info_line(4, connected ? "BT ON" : "BT OFF", 0, 0);
    info_line(5, "ACCESO ", up / 3600, "H");
    info_line(6, "       ", up / 60 % 60, "M");
    info_line(8, "PUNTO ", morse_dot_ms(), "MS");
    info_line(9, "LINEA ", morse_dash_ms(), "MS");
    info_line(11, rtc_is_set() ? "ORA OK" : "ORA DA FARE", 0, 0);
    draw_touch_mark(pressed);
}

static void draw_msg(void) {
    int y = 16, i = 0, col;
    char line[14];
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    center_text(3, "MESSAGGIO", 1, C_YELLOW);
    while (msg_text[i] && y < lcd.height - 16) {
        for (col = 0; col < 13 && msg_text[i]; col++) {
            char c = msg_text[i++];
            line[col] = (c >= 32 && c < 127) ? c : '?';
        }
        line[col] = 0;
        font_text(1, y, line, 1, C_WHITE, C_BLACK);
        y += 10;
    }
    draw_touch_mark(pressed);
}

void ui_redraw(void) {
    if (!screen_on) return;
    switch (screen) {
    case S_FACE: draw_face(); break;
    case S_MENU: draw_menu(); break;
    case S_INFO: draw_info(); break;
    case S_MSG: draw_msg(); break;
    }
}

/* ------------------------------------------------------------------ behaviour */

static void go(int s) {
    screen = s;
    if (s == S_FACE) morse_reset();
    ui_redraw();
}

static void wake(void) {
    screen_on = 1;
    lcd_backlight(1);
    ui_redraw();
}

static void sleep_screen(void) {
    screen_on = 0;
    lcd_backlight(0);
    screen = S_FACE;
    morse_reset();
}

static void on_access(void) {
    if (cb->buzz) {
        cb->buzz(60);
        sleep_us(80 * 1000);
        cb->buzz(60);
    }
    menu_top = 0;
    go(S_MENU);
}

static void menu_select(void) {
    if (menu_top == M_INFO) go(S_INFO);
    else go(S_FACE);
}

static const morse_cb_t morse_callbacks = {0, draw_pattern, draw_word, on_access};
static morse_cb_t morse_cb;

void ui_init(const ui_cb_t *c) {
    cb = c;
    morse_cb = morse_callbacks;
    morse_cb.notify = c->notify;
    morse_init(&morse_cb);
    battery_update();
    t_activity = clock_time();
    lcd_backlight(1);
    ui_redraw();
}

void ui_set_connected(int on) { connected = on; }
u32 ui_press_ms(void) { return pressed ? ms_since(t_press) : 0; }
int ui_battery_level(void) { return battery_level(); }

void ui_find(void) {
    int i;
    if (!screen_on) wake();
    t_activity = clock_time();
    for (i = 0; i < 3 && cb->buzz; i++) {
        cb->buzz(200);
        sleep_us(150 * 1000);
    }
}

void ui_message(const char *text) {
    int i;
    for (i = 0; text[i] && i < (int)sizeof(msg_text) - 1; i++) msg_text[i] = text[i];
    msg_text[i] = 0;
    screen = S_MSG;
    if (!screen_on) {
        screen_on = 1;
        lcd_backlight(1);
    }
    ui_redraw();
    t_activity = clock_time();
    if (cb->buzz) {
        cb->buzz(150);
        sleep_us(120 * 1000);
        cb->buzz(150);
    }
}

void ui_poll(int touched) {
    rtc_poll();

    if (touched != pressed) {               /* key edge */
        pressed = touched;
        t_activity = clock_time();
        if (touched) {
            t_press = clock_time();
            long_fired = 0;
            consumed = 0;
            if (!screen_on) {               /* this touch only wakes the face */
                screen = S_FACE;
                wake();
                consumed = 1;
            }
        } else if (!consumed) {
            if (screen == S_MENU) {
                if (long_fired) menu_select();
                else {
                    menu_top = (menu_top + 1) % M_COUNT;
                    draw_menu();
                }
            } else if (screen == S_INFO) {
                go(S_MENU);
            } else if (screen == S_MSG) {
                go(S_FACE);
            }
        }
        draw_touch_mark(touched);
    }

    /* long-press feedback in the menu: a short buzz when the threshold is passed */
    if (pressed && !consumed && !long_fired && screen == S_MENU && ms_since(t_press) > LONG_MS) {
        long_fired = 1;
        if (cb->buzz) cb->buzz(25);
    }

    /* Morse only on the watch face, and not for the touch that woke the screen */
    morse_poll(screen == S_FACE && screen_on && !consumed ? touched : 0);

    /* periodic updates */
    if (rtc_uptime() - last_battery_s >= BATTERY_EVERY_S) {
        last_battery_s = rtc_uptime();
        battery_update();
    }
    if (screen_on && screen == S_FACE) {
        rtc_time_t t;
        rtc_get(&t);
        if (t.min != last_min) draw_time();
        if (battery_level() != last_level || battery_charging() != last_charging ||
            connected != last_conn)
            draw_status();
    }

    /* screen timeout */
    if (screen_on && !pressed) {
        u32 limit = screen == S_FACE ? FACE_TIMEOUT_MS : screen == S_MSG ? MSG_TIMEOUT_MS
                                                                          : OTHER_TIMEOUT_MS;
        if (ms_since(t_activity) > limit) sleep_screen();
    }
}
