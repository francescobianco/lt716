/*
 * MorseOS framework: active app, menus built from the bundle, touch-key routing,
 * Morse feeding, screen timeout, touch mark. Same behaviour as MorseOS 0.4's ui.c.
 */
#include "mos.h"
#include "font.h"
#include "morse.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define LONG_MS         600   /* apps with MOS_F_LONG */
#define MENU_SHORT_MS   300   /* menu: a tap shorter than this moves to the next box */
#define MENU_CONFIRM_MS 800   /* menu: holding this long fills the gauge and opens the box */
#define GAUGE_X         7     /* gauge along the bottom edge inside the selected box */
#define GAUGE_Y         (8 + 66 - 7)
#define GAUGE_W         66
#define GAUGE_H         4
#define HOLD_EXIT_MS    3000
#define DEFAULT_TIMEOUT 30
#define MAX_ITEMS       12

/* a box in a menu: an app, a submenu, or "back" */
typedef struct {
    const char *label;
    const mos_app_t *app;  /* when it opens an app */
    const char *submenu;   /* when it opens a submenu ("MENU" = main menu) */
    u8 home;               /* back to the watch face */
    u8 back;               /* drawn as a back arrow instead of its label */
} item_t;

static void (*buzz_cb)(int ms);
static void (*notify_cb)(const u8 *pkt, int len);
static const mos_app_t *home_app, *active;
static int in_menu;                 /* the menu is shown instead of the active app */
static const char *menu_name = "MENU";
static const char *app_menu = "MENU";  /* menu the active app was opened from */
static item_t items[MAX_ITEMS];
static int n_items, menu_top;
static int screen_on = 1, pressed, consumed, long_fired, connected, rssi_dbm = -127;
static char peer[4];
static u32 t_press, t_activity;
static int gauge_fill;  /* pixels of the menu gauge drawn so far */

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

u32 mos_ms_since(u32 t) { return (clock_time() - t) / (sys_tick_per_us * 1000); }
void mos_buzz(int ms) { if (buzz_cb) buzz_cb(ms); }
void mos_notify(const u8 *pkt, int len) { if (notify_cb) notify_cb(pkt, len); }
void mos_set_connected(int on) {
    connected = on;
    if (!on) peer[0] = 0;
}

void mos_set_peer(const char *name) {
    int n = 0;
    for (; name && *name && n < 3; name++) {
        char c = *name;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) peer[n++] = c;
    }
    peer[n] = 0;
}

const char *mos_peer(void) { return peer; }
void mos_set_rssi(int dbm) { rssi_dbm = dbm; }
int  mos_rssi(void) { return rssi_dbm; }

int mos_signal_bars(void) {
    if (!connected) return 0;
    if (rssi_dbm >= -60) return 4;
    if (rssi_dbm >= -70) return 3;
    if (rssi_dbm >= -80) return 2;
    if (rssi_dbm >= -90) return 1;
    return 0;
}
int  mos_connected(void) { return connected; }
u32  mos_press_ms(void) { return pressed ? mos_ms_since(t_press) : 0; }
int  mos_screen_on(void) { return screen_on; }
int  mos_active(const mos_app_t *a) { return !in_menu && active == a; }

/* ------------------------------------------------------------------ helpers */

char *mos_num(char *p, u32 v, int width) {
    char t[10];
    int n = 0;
    do t[n++] = '0' + v % 10, v /= 10; while (v);
    while (n < width) t[n++] = '0';
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

void mos_center_text(int y, const char *s, int scale, u16 fg) {
    font_text((lcd.width - font_width(s, scale)) / 2, y, s, scale, fg, C_BLACK);
}

/* '.' dot, '-' dash in col; '*' / '=' the element being pressed (tutor), in white */
void mos_pattern_at(int y, const char *pat, u16 col, int centered) {
    int x = 4, w = 0;
    const char *q;
    if (centered) {
        for (q = pat; *q; q++) w += (*q == '.' || *q == '*' ? 5 : 12) + 3;
        x = (lcd.width - w + 3) / 2;
    }
    lcd_fill(0, y, lcd.width, 8, C_BLACK);
    for (; *pat; pat++) {
        int live = *pat == '*' || *pat == '=';
        int ew = *pat == '.' || *pat == '*' ? 5 : 12;
        lcd_fill(x, y + 1, ew, 6, live ? C_WHITE : col);
        x += ew + 3;
    }
}

static void touch_mark(int on) {
    lcd_fill(lcd.width / 2 - 4, lcd.height - 7, 8, 5, on ? C_YELLOW : C_GREY);
}

/* ------------------------------------------------------------------ menus */

static void add_item(const char *label, const mos_app_t *app, const char *submenu, int home) {
    if (n_items == MAX_ITEMS) return;
    items[n_items].label = label;
    items[n_items].app = app;
    items[n_items].submenu = submenu;
    items[n_items].home = home;
    items[n_items].back = 0;
    n_items++;
}

static void add_back(const char *submenu, int home) {
    add_item("", 0, submenu, home);
    if (n_items) items[n_items - 1].back = 1;
}

static void build_menu(const char *name) {
    int i, j;
    n_items = 0;
    for (i = 0; i < mos_entry_count; i++)
        if (str_eq(mos_entries[i].menu, name)) add_item(mos_entries[i].label, mos_entries[i].app, 0, 0);
    if (str_eq(name, "MENU")) {
        /* one box per submenu, in the order they first appear */
        for (i = 0; i < mos_entry_count; i++) {
            const char *m = mos_entries[i].menu;
            int seen = 0;
            if (!m[0] || str_eq(m, "MENU")) continue;
            for (j = 0; j < i; j++)
                if (str_eq(mos_entries[j].menu, m)) seen = 1;
            if (!seen) add_item(m, 0, m, 0);
        }
        add_back(0, 1);            /* to the watch face */
    } else {
        add_back("MENU", 0);       /* to the main menu */
    }
}

/* back arrow, 32 x 23 px, centred on (cx, cy): triangle head pointing left + shaft */
static void draw_back_arrow(int cx, int cy, u16 c) {
    int x0 = cx - 16, i;
    for (i = 0; i < 12; i++) lcd_fill(x0 + i, cy - i, 1, 2 * i + 1, c);
    lcd_fill(x0 + 12, cy - 3, 20, 7, c);
}

static void draw_box(int y, const item_t *it, int selected) {
    const char *label = it->label;
    u16 c = selected ? C_WHITE : C_GREY;
    int x = 4, w = 72, h = 66, t = selected ? 2 : 1, sc;
    lcd_fill(x, y, w, h, C_BLACK);
    lcd_fill(x, y, w, t, c);
    lcd_fill(x, y + h - t, w, t, c);
    lcd_fill(x, y, t, h, c);
    lcd_fill(x + w - t, y, t, h, c);
    if (it->back) {
        draw_back_arrow(x + w / 2, y + h / 2 - 2, c);
        return;
    }
    sc = font_width(label, 2) <= w - 6 ? 2 : 1;
    font_text(x + (w - font_width(label, sc)) / 2, y + (h - 8 * sc) / 2, label, sc, c, C_BLACK);
}

/* the menu gauge: fill grows while the key is held on the selected box */
static void draw_gauge(int fill) {
    if (fill > GAUGE_W) fill = GAUGE_W;
    if (fill > gauge_fill) lcd_fill(GAUGE_X + gauge_fill, GAUGE_Y, fill - gauge_fill, GAUGE_H, C_GREEN);
    else if (fill < gauge_fill) lcd_fill(GAUGE_X + fill, GAUGE_Y, gauge_fill - fill, GAUGE_H, C_BLACK);
    gauge_fill = fill;
}

static void draw_menu(void) {
    gauge_fill = 0;
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    draw_box(8, &items[menu_top], 1);
    if (n_items > 1) draw_box(80, &items[(menu_top + 1) % n_items], 0);
    touch_mark(pressed);
}

static void menu_select(void) {
    item_t *it = &items[menu_top];
    if (it->home) mos_home();
    else if (it->submenu) mos_menu(it->submenu);
    else if (it->app) {
        app_menu = menu_name;
        mos_open(it->app);
    }
}

/* ------------------------------------------------------------------ navigation */

void mos_redraw(void) {
    if (in_menu) draw_menu();
    else if (active && active->draw) {
        active->draw();
        touch_mark(pressed);
    }
}

void mos_open(const mos_app_t *app) {
    morse_reset();
    in_menu = 0;
    active = app;
    if (app->enter) app->enter();
    mos_redraw();
}

void mos_home(void) {
    app_menu = "MENU";
    if (home_app) mos_open(home_app);
}

void mos_menu(const char *name) {
    morse_reset();
    menu_name = name;
    build_menu(name);
    menu_top = 0;
    in_menu = 1;
    mos_redraw();
}

void mos_back(void) { mos_menu(app_menu); }

/* the image is always up to date (drawn even in the dark): waking is just the backlight */
void mos_wake(void) {
    if (!screen_on) {
        screen_on = 1;
        lcd_sleep(0);
        lcd_backlight(1);
    }
    t_activity = clock_time();
}

static void sleep_screen(void) {
    screen_on = 0;
    lcd_backlight(0);
    app_menu = "MENU";
    if (home_app) mos_open(home_app);  /* drawn now, in the dark, ready for the next wake */
    else mos_menu("MENU");
    lcd_sleep(1);                      /* panel asleep, image kept */
}

static void red_line(int y, const char *s, u16 col) {
    font_text((lcd.width - font_width(s, 1)) / 2, y, s, 1, col, C_RED);
}

/* stage 0 = gone (redraw), 1 = key still held, 2 = released: taps counted so far */
void mos_emergency_warning(int stage, int taps) {
    if (!stage) {
        mos_redraw();
        return;
    }
    if (stage == 1) {
        lcd_fill(0, 0, lcd.width, lcd.height, C_RED);
        red_line(40, "AGGIORNAMENTO", C_WHITE);
        red_line(64, "RILASCIA", C_YELLOW);
        red_line(76, "E TOCCA", C_YELLOW);
        red_line(88, "3 VOLTE", C_YELLOW);
        red_line(120, "ALTRO:", C_WHITE);
        red_line(132, "ANNULLA", C_WHITE);
        mos_wake();
    } else {
        int i;
        lcd_fill(0, 100, lcd.width, 12, C_RED);
        for (i = 0; i < 3; i++)
            lcd_fill(16 + i * 18, 102, 12, 8, i < taps ? C_WHITE : C_GREY);
    }
}

/* ------------------------------------------------------------------ Morse -> active app */

static int morse_target(void) { return !in_menu && active && (active->flags & MOS_F_MORSE); }

static void m_pattern(const char *p) {
    if (morse_target() && active->pattern) active->pattern(p);
}
static void m_word(const char *w) {
    if (morse_target() && active->word) active->word(w);
}
static void m_access(void) {
    if (morse_target() && active->access) active->access();
}
static void m_letter(char c) {
    if (morse_target() && active->letter) active->letter(c);
}

static void m_erased(void) {
    if (!screen_on || !morse_target()) return;
    mos_buzz(25);
    sleep_us(60 * 1000);
    mos_buzz(25);
}

static morse_cb_t morse_cb = {0, m_pattern, m_word, m_access, m_letter, m_erased};

/* ------------------------------------------------------------------ init / poll */

void mos_init(void (*buzz)(int ms), void (*notify)(const u8 *pkt, int len)) {
    int i;
    buzz_cb = buzz;
    notify_cb = notify;
    morse_cb.notify = notify;
    morse_init(&morse_cb);
    for (i = 0; i < mos_entry_count; i++)
        if (mos_entries[i].app->flags & MOS_F_HOME) home_app = mos_entries[i].app;
    t_activity = clock_time();
    lcd_backlight(1);
    if (home_app) mos_open(home_app);
    else mos_menu("MENU");
}

void mos_poll(int touched) {
    int flags = (!in_menu && active) ? active->flags : MOS_F_LONG;

    if (touched != pressed) {                       /* key edge */
        pressed = touched;
        t_activity = clock_time();
        if (touched) {
            t_press = clock_time();
            long_fired = consumed = 0;
            if (!screen_on) {                       /* this touch only wakes the home screen, */
                touch_mark(1);                      /* already drawn: just the backlight */
                mos_wake();
                consumed = 1;
            }
        } else if (!consumed) {
            if (in_menu) {
                if (mos_ms_since(t_press) < MENU_SHORT_MS && n_items) {
                    menu_top = (menu_top + 1) % n_items;  /* tap: next box */
                    draw_menu();
                } else {
                    draw_gauge(0);                        /* let go too early: reset */
                }
            } else if (active && active->key) {
                active->key(long_fired ? MOS_KEY_LONG : MOS_KEY_SHORT);
            }
        }
        touch_mark(touched);
    }

    if (pressed && !consumed && in_menu && screen_on) {
        u32 held = mos_ms_since(t_press);
        if (held >= MENU_CONFIRM_MS) {                    /* gauge full: open it now */
            draw_gauge(GAUGE_W);
            consumed = 1;
            mos_buzz(40);
            menu_select();
        } else if (held >= MENU_SHORT_MS) {
            draw_gauge((held - MENU_SHORT_MS) * GAUGE_W / (MENU_CONFIRM_MS - MENU_SHORT_MS));
        }
    } else if (pressed && !consumed) {
        u32 held = mos_ms_since(t_press);
        if (!long_fired && (flags & MOS_F_LONG) && held > LONG_MS) {
            long_fired = 1;
            mos_buzz(25);
            if (!in_menu && active && active->key) active->key(MOS_KEY_HOLD);
        }
        if (!in_menu && (flags & MOS_F_HOLD_EXIT) && held > HOLD_EXIT_MS) {
            consumed = 1;
            mos_buzz(60);
            mos_back();
        }
    }

    morse_poll(screen_on && morse_target() && !consumed ? touched : 0);

    /* the home screen keeps itself up to date in the dark too */
    if (!in_menu && active && active->tick && (screen_on || active == home_app)) active->tick();

    if (screen_on && !pressed) {
        u32 limit = 1000 * (u32)((!in_menu && active && active->timeout_s) ? active->timeout_s
                                                                          : DEFAULT_TIMEOUT);
        if (mos_ms_since(t_activity) > limit) sleep_screen();
    }
}
