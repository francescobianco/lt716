/*
 * IMPOSTAZIONI: a list of properties with their value.
 *   tap        next property (in edit mode: next value)
 *   hold       edit the selected value / confirm it and go back to the list;
 *              on INDIETRO: back to the menu
 * While the key is held a green gauge fills along the bottom of the selected row (the
 * hold-to-confirm paradigm of the menus). Values live in RAM for now: lost on reboot.
 */
#include "mos.h"
#include "font.h"
#include "morse.h"
#include "st7735.h"

#define ROW_Y0   18
#define ROW_H    30
#define HOLD_MS  600   /* MOS_F_LONG threshold in core/ui/mos.c */

typedef struct {
    const char *label;
    int (*get)(void);
    void (*next)(void);          /* tap in edit mode */
    void (*show)(char *out, int v);
} prop_t;

static int get_speed(void) { return morse_speed(); }
static void next_speed(void) { morse_set_speed(morse_speed() % 16 + 1); }
static void show_num(char *out, int v) { mos_num(out, v, 1); }

static int get_vibra(void) { return mos_vibration(); }
static void next_vibra(void) { mos_set_vibration(!mos_vibration()); }
static void show_yesno(char *out, int v) {
    const char *s = v ? "SI" : "NO";
    while ((*out++ = *s++));
}

static const prop_t props[] = {
    {"VELOCITA", get_speed, next_speed, show_num},
    {"VIBRAZIONE", get_vibra, next_vibra, show_yesno},
    {"INDIETRO", 0, 0, 0},
};
#define N_PROPS ((int)(sizeof(props) / sizeof(props[0])))

static int sel, editing, gauge, held;

static void draw_row(int i) {
    int y = ROW_Y0 + i * ROW_H, on = i == sel;
    u16 fg = on ? C_WHITE : C_GREY;
    char v[12];
    lcd_fill(0, y, lcd.width, ROW_H, C_BLACK);
    if (on) {                                   /* frame around the selected row */
        lcd_fill(2, y, lcd.width - 4, 1, fg);
        lcd_fill(2, y + ROW_H - 2, lcd.width - 4, 1, fg);
        lcd_fill(2, y, 1, ROW_H - 1, fg);
        lcd_fill(lcd.width - 3, y, 1, ROW_H - 1, fg);
    }
    if (!props[i].get) {                        /* INDIETRO */
        mos_center_text(y + (ROW_H - 8) / 2 - 1, props[i].label, 1, fg);
        return;
    }
    font_text(6, y + 4, props[i].label, 1, fg, C_BLACK);
    props[i].show(v, props[i].get());
    font_text(lcd.width - 6 - font_width(v, 2), y + 13, v, 2,
              on && editing ? C_YELLOW : fg, C_BLACK);
}

static void draw_hint(void) {
    lcd_fill(0, 132, lcd.width, lcd.height - 132, C_BLACK);
    mos_center_text(136, editing ? "TAP: CAMBIA" : "TAP: SCORRI", 1, C_GREY);
    mos_center_text(146, editing ? "TIENI: OK" : "TIENI: MODIFICA", 1, C_GREY);
}

static void draw_gauge(int fill) {
    int y = ROW_Y0 + sel * ROW_H + ROW_H - 5, w = lcd.width - 12;
    if (fill > w) fill = w;
    if (fill > gauge) lcd_fill(6 + gauge, y, fill - gauge, 2, C_GREEN);
    else if (fill < gauge) lcd_fill(6 + fill, y, gauge - fill, 2, C_BLACK);
    gauge = fill;
}

static void enter(void) {
    sel = editing = gauge = held = 0;
}

static void draw(void) {
    int i;
    lcd_fill(0, 0, lcd.width, ROW_Y0, C_BLACK);
    mos_center_text(4, "IMPOSTAZIONI", 1, C_YELLOW);
    for (i = 0; i < N_PROPS; i++) draw_row(i);
    lcd_fill(0, ROW_Y0 + N_PROPS * ROW_H, lcd.width, 132 - (ROW_Y0 + N_PROPS * ROW_H), C_BLACK);
    draw_hint();
    gauge = 0;
}

/* the gauge follows the key while it is held, until the long threshold */
static void tick(void) {
    u32 ms = mos_press_ms();
    if (ms && !held) draw_gauge((int)(ms * (lcd.width - 12) / HOLD_MS));
    else if (!ms && gauge) draw_gauge(0);
    if (!ms) held = 0;
}

static void key(int ev) {
    int prev = sel;
    if (ev == MOS_KEY_SHORT) {
        draw_gauge(0);
        if (editing) props[sel].next();
        else sel = (sel + 1) % N_PROPS;
        if (prev != sel) draw_row(prev);
        draw_row(sel);
    } else if (ev == MOS_KEY_HOLD) {
        held = 1;
        gauge = 0;                          /* the row redraw below clears it */
        if (!props[sel].get) {
            mos_back();
            return;
        }
        editing = !editing;
        draw_row(sel);
        draw_hint();
    }
}

const mos_app_t app_settings = {
    "settings", MOS_F_LONG, 30, enter, draw, tick, key, 0, 0, 0, 0,
};
