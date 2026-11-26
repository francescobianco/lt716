/*
 * STRUMENTI > LUCE: a torch. The screen white at full backlight and, on the second band, the
 * green LED under it too.
 *   tap         on -> SOS -> off -> on ...  (SOS: ... --- ... in Morse, screen and LED blink)
 *   long press  back
 * The screen does not time out while LUCE is open.
 */
#include "mos.h"
#include "aafont.h"
#include "greenled.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define UNIT_MS 200   /* Morse unit: dot 1, dash 3, gap 1, letter gap 3, word gap 7 */

enum { ON, SOS, OFF };
static int mode, lit, step, steps;
static u32 t_step;
static u8 seq[40];    /* units of each step; even steps lit, odd steps dark */

static void build_sos(void) {
    static const char sos[] = "...---...";
    int i, n = 0;
    for (i = 0; sos[i]; i++) {
        seq[n++] = sos[i] == '.' ? 1 : 3;          /* lit */
        seq[n++] = i == 8 ? 7 : (i % 3 == 2) ? 3 : 1;   /* dark: word, letter or element gap */
    }
    steps = n;
}

static void light(int on) {
    lcd_backlight(on);
    greenled_set(on);
    lit = on;
}

static void draw_off(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    aafont_line(&aafont_medium, 60, "LUCE", C_GREY, C_BLACK);
    aafont_line(&aafont_small, 84, "TAP ACCENDI", C_GREY, C_BLACK);
    aafont_line(&aafont_small, 96, "TIENI ESCI", C_GREY, C_BLACK);
    lcd_backlight(1);
    greenled_set(0);
}

static void enter(void) {
    mode = ON;
    build_sos();
}

static void draw(void) {
    if (mode == OFF) {
        draw_off();
        return;
    }
    lcd_fill(0, 0, lcd.width, lcd.height, C_WHITE);
    light(1);
    step = 0;
    t_step = clock_time();
}

static void tick(void) {
    mos_wake();                                     /* no screen timeout while open */
    if (mode == ON) {
        greenled_set(1);                            /* keep the LED alive */
    } else if (mode == SOS) {
        if (mos_ms_since(t_step) >= (u32)seq[step] * UNIT_MS) {
            t_step = clock_time();
            step = (step + 1) % steps;
            light(!(step & 1));
        } else {
            greenled_set(lit);
        }
    } else {
        greenled_set(0);
    }
}

static void key(int ev) {
    if (ev == MOS_KEY_HOLD) {                       /* long press: out, all off */
        greenled_set(0);
        lcd_backlight(1);
        mos_back();
        return;
    }
    if (ev != MOS_KEY_SHORT) return;
    mode = (mode + 1) % 3;
    mos_redraw();
}

const mos_app_t app_torch = {"torch", MOS_F_LONG, 0, enter, draw, tick, key, 0, 0, 0, 0, 0};
