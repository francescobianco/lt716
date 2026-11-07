/*
 * PRATICA: a target letter and its code; type it in Morse. OK = a point and the next
 * letter, NO = the letter typed, same target. Hold the key 3 s to leave.
 */
#include "mos.h"
#include "font.h"
#include "morse.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define NEXT_TARGET_MS 700

static char target;
static int score, streak, next_pending;
static u32 t_next;

static void new_target(void) {
    static const char letters[] = "ETIANMSURWDKGOHVFLPJBXCYZQ";
    char prev = target;
    do target = letters[(clock_time() >> 4) % 26]; while (target == prev);
}

static void draw_target(void) {
    char t[2] = {target, 0};
    const char *code = morse_code_of(target);
    lcd_fill(0, 18, lcd.width, 62, C_BLACK);
    mos_center_text(22, t, 6, C_WHITE);
    mos_pattern_at(74, code ? code : "", C_GREY, 1);
}

static void draw_score(void) {
    char t[16], *p = t;
    *p++ = 'P';
    *p++ = ':';
    p = mos_num(p, score, 1);
    *p++ = ' ';
    *p++ = 'S';
    *p++ = ':';
    mos_num(p, streak, 1);
    lcd_fill(0, 2, lcd.width, 8, C_BLACK);
    font_text(3, 2, t, 1, C_GREY, C_BLACK);
}

static void draw_result(const char *s, u16 col) {
    lcd_fill(0, 108, lcd.width, 16, C_BLACK);
    if (s) mos_center_text(108, s, 2, col);
}

static void enter(void) {
    new_target();
    score = streak = next_pending = 0;
}

static void draw(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    draw_score();
    lcd_fill(4, 13, lcd.width - 8, 1, C_GREY);
    draw_target();
    mos_center_text(138, "TIENI 3S", 1, C_GREY);
    mos_center_text(147, "PER USCIRE", 1, C_GREY);
}

static void tick(void) {
    if (next_pending && mos_ms_since(t_next) > NEXT_TARGET_MS) {
        next_pending = 0;
        new_target();
        draw_target();
        draw_result(0, 0);
    }
}

static void pattern(const char *pat) { mos_pattern_at(96, pat, C_YELLOW, 1); }

static void letter(char c) {
    char t[6] = {'N', 'O', ' ', c, 0};
    if (next_pending) return;
    if (c == target) {
        score++;
        streak++;
        draw_result("OK", C_GREEN);
        mos_buzz(40);
        next_pending = 1;
        t_next = clock_time();
    } else {
        streak = 0;
        if (c < 'A' || c > 'Z') t[3] = '?';
        draw_result(t, C_RED);
        mos_buzz(200);
    }
    draw_score();
}

const mos_app_t app_practice = {
    "practice", MOS_F_MORSE | MOS_F_HOLD_EXIT, 30, enter, draw, tick, 0, letter, pattern, 0, 0,
};
