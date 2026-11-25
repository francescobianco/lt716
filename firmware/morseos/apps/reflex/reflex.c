/*
 * GIOCHI > RIFLESSI: the smallest game. Tap to start, wait for the green VAI!, touch as fast
 * as you can: the reaction time in ms, and the best of the session. Touching before the green
 * is PRESTO!. Hold the key 3 s to leave. A placeholder for the GIOCHI menu.
 */
#include "mos.h"
#include "font.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

enum { READY, WAIT, GO, DONE };

#define PAD_Y 44
#define PAD_H 60

static int state, best, was_down;
static u32 t_go, wait_ms;

static void pad(u16 bg, const char *a, const char *b, u16 fg) {
    lcd_fill(0, PAD_Y, lcd.width, PAD_H, bg);
    int sc = a && font_width(a, 2) > lcd.width ? 1 : 2;  /* ASPETTA, PRESTO!: 84 px at 2x */
    if (a) font_text((lcd.width - font_width(a, sc)) / 2, PAD_Y + (b ? 14 : 22) + (2 - sc) * 4,
                     a, sc, fg, bg);
    if (b) font_text((lcd.width - font_width(b, 1)) / 2, PAD_Y + 36, b, 1, fg, bg);
}

static void draw_best(void) {
    char t[16] = "RECORD ", *p = t + 7;
    lcd_fill(0, 116, lcd.width, 10, C_BLACK);
    if (!best) return;
    p = mos_num(p, best, 1);
    *p++ = 'M';
    *p++ = 'S';
    *p = 0;
    mos_center_text(116, t, 1, C_YELLOW);
}

static void enter(void) {
    state = READY;
    was_down = 1;  /* the press that opened the game is not a move */
}

static void draw(void) {
    lcd_fill(0, 0, lcd.width, PAD_Y, C_BLACK);
    mos_center_text(16, "RIFLESSI", 1, C_YELLOW);
    pad(C_GREY, "TOCCA", "PER INIZIARE", C_WHITE);
    lcd_fill(0, PAD_Y + PAD_H, lcd.width, lcd.height - PAD_Y - PAD_H, C_BLACK);
    draw_best();
    mos_center_text(138, "TIENI 3S", 1, C_GREY);
    mos_center_text(147, "PER USCIRE", 1, C_GREY);
}

/* the press is seen when it starts (in tick), not on release: that is the reaction */
static void tick(void) {
    int down = mos_press_ms() > 0, edge = down && !was_down;
    was_down = down;
    if (state == WAIT && mos_ms_since(t_go) >= wait_ms) {
        state = GO;
        t_go = clock_time();
        pad(C_GREEN, "VAI!", 0, C_BLACK);
        return;
    }
    if (!edge) return;
    if (state == READY || state == DONE) {
        state = WAIT;
        t_go = clock_time();
        wait_ms = 1500 + (clock_time() >> 6) % 2500;
        pad(C_RED, "ASPETTA", 0, C_WHITE);
    } else if (state == WAIT) {
        state = DONE;
        mos_buzz(200);
        pad(C_BLACK, "PRESTO!", "RIPROVA", C_RED);
    } else if (state == GO) {
        char t[12];
        int ms = mos_ms_since(t_go);
        state = DONE;
        mos_buzz(40);
        if (!best || ms < best) best = ms;
        mos_num(t, ms, 1);
        pad(C_BLACK, t, "MILLISECONDI", C_WHITE);
        draw_best();
    }
}

const mos_app_t app_reflex = {
    "reflex", MOS_F_HOLD_EXIT, 30, enter, draw, tick, 0, 0, 0, 0, 0, 0,
};
