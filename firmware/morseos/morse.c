/*
 * MorseOS input: the touch key output follows the finger, so every press and gap is
 * measured exactly. Calibrated on the user's own rhythm (touchlog of 2026-11-07: dots
 * 120-200 ms, dashes 600-1400 ms, gaps inside a letter <= 415 ms, between letters 1-4 s):
 *   press <  30 ms                     ignored (glitch)
 *   press <  (dot + dash) / 2          dot   (running averages of the user's dot and dash)
 *   press >  max(2500, 2.5 dash) ms    cancel the word
 *   gap   >  max(600, 3 dot) ms        end of letter; as soon as the word ends with TAN: access
 *   gap   >  WORD_END_MS               the word is dropped
 * Every press and every decoded letter/word is reported as a DATA notification:
 *   FD FF FF FF 'T' <elem> <press ms u16> <gap ms u16> <dot ms u16> <dash ms u16>
 *   FD FF FF FF 'L' <letter> <pattern...>
 *   FD FF FF FF 'W' <word...>
 */
#include "morse.h"
#include "drivers/5316/clock.h"

#define GLITCH_MS     30
#define CANCEL_MIN_MS 2500
#define LETTER_MIN_MS 600
#define WORD_END_MS   4000
#define DOT_INIT      150
#define DASH_INIT     700
#define MAX_PATTERN   7
#define MAX_WORD      8

static const char *const table[] = {
    "A.-", "B-...", "C-.-.", "D-..", "E.", "F..-.", "G--.", "H....", "I..", "J.---",
    "K-.-", "L.-..", "M--", "N-.", "O---", "P.--.", "Q--.-", "R.-.", "S...", "T-",
    "U..-", "V...-", "W.--", "X-..-", "Y-.--", "Z--..",
    "0-----", "1.----", "2..---", "3...--", "4....-", "5.....", "6-....", "7--...",
    "8---..", "9----.", 0,
};

static const morse_cb_t *cb;
static u32 dot_ms = DOT_INIT, dash_ms = DASH_INIT;
static int pressed;
static u32 t_edge, last_gap_ms;
static char pattern[MAX_PATTERN + 1];
static int npat;
static char word[MAX_WORD + 1];
static int nword;

static u32 ms_since(u32 t) { return (clock_time() - t) / (sys_tick_per_us * 1000); }

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static char decode(const char *pat) {
    int i;
    for (i = 0; table[i]; i++)
        if (str_eq(table[i] + 1, pat)) return table[i][0];
    return '?';
}

static void send(char kind, const u8 *p, int n) {
    u8 pkt[20] = {0xFD, 0xFF, 0xFF, 0xFF};
    int i;
    pkt[4] = kind;
    for (i = 0; i < n && i < 15; i++) pkt[5 + i] = p[i];
    if (cb->notify) cb->notify(pkt, sizeof(pkt));
}

void morse_init(const morse_cb_t *c) {
    cb = c;
    t_edge = clock_time();
}

void morse_reset(void) {
    npat = 0;
    pattern[0] = 0;
    nword = 0;
    word[0] = 0;
    if (cb->pattern) cb->pattern(pattern);
    if (cb->word) cb->word(word);
}

u32 morse_dot_ms(void) { return dot_ms; }
u32 morse_dash_ms(void) { return dash_ms; }

static int ends_with_access(void) {
    int n = 0, i;
    while (MORSE_ACCESS_WORD[n]) n++;
    if (nword < n) return 0;
    for (i = 0; i < n; i++)
        if (word[nword - n + i] != MORSE_ACCESS_WORD[i]) return 0;
    return 1;
}

static void end_letter(void) {
    u8 msg[1 + MAX_PATTERN];
    int i;
    char c = decode(pattern);
    msg[0] = c;
    for (i = 0; i < npat; i++) msg[1 + i] = pattern[i];
    send('L', msg, 1 + npat);
    if (nword == MAX_WORD) {  /* keep the last letters: TAN may follow a typo */
        for (i = 1; i < MAX_WORD; i++) word[i - 1] = word[i];
        nword--;
    }
    word[nword++] = c;
    word[nword] = 0;
    npat = 0;
    pattern[0] = 0;
    if (cb->pattern) cb->pattern(pattern);
    if (cb->word) cb->word(word);
    if (ends_with_access()) {
        send('W', (const u8 *)word, nword);
        morse_reset();
        if (cb->access) cb->access();
    }
}

void morse_poll(int touched) {
    u32 dt = ms_since(t_edge);

    if (touched != pressed) {
        if (touched) {
            last_gap_ms = dt;          /* press starts: dt was a gap */
        } else {                       /* press ends: dt was its length */
            u8 msg[9];
            char e;
            u32 cancel_ms = 5 * dash_ms / 2 < CANCEL_MIN_MS ? CANCEL_MIN_MS : 5 * dash_ms / 2;
            if (dt < GLITCH_MS) {
                pressed = 0;
                return;
            }
            if (dt > cancel_ms) {
                morse_reset();
                e = 'X';
            } else if (dt < (dot_ms + dash_ms) / 2) {
                e = '.';
                dot_ms = (3 * dot_ms + dt) / 4;
            } else {
                e = '-';
                dash_ms = (3 * dash_ms + dt) / 4;
            }
            if (dot_ms < 50) dot_ms = 50;
            if (dot_ms > 500) dot_ms = 500;
            if (dash_ms < 2 * dot_ms) dash_ms = 2 * dot_ms;
            if (dash_ms > 3000) dash_ms = 3000;
            if (e != 'X' && npat < MAX_PATTERN) {
                pattern[npat++] = e;
                pattern[npat] = 0;
                if (cb->pattern) cb->pattern(pattern);
            }
            msg[0] = e;
            msg[1] = dt;
            msg[2] = dt >> 8;
            msg[3] = last_gap_ms;
            msg[4] = last_gap_ms >> 8;
            msg[5] = dot_ms;
            msg[6] = dot_ms >> 8;
            msg[7] = dash_ms;
            msg[8] = dash_ms >> 8;
            send('T', msg, sizeof(msg));
        }
        pressed = touched;
        t_edge = clock_time();
        return;
    }

    if (!pressed) {
        u32 letter_gap = 3 * dot_ms < LETTER_MIN_MS ? LETTER_MIN_MS : 3 * dot_ms;
        if (npat && dt > letter_gap) end_letter();
        if (nword && !npat && dt > WORD_END_MS) {
            send('W', (const u8 *)word, nword);
            morse_reset();
        }
    }
}
