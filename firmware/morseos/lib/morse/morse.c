/*
 * MorseOS input: the touch key output follows the finger, so every press and gap is
 * measured exactly. The timing comes from a speed level 1 (very slow) .. 16 (very fast),
 * set over BLE (core/system command 0x30):
 *   press <  30 ms                 ignored (glitch)
 *   press <  threshold             dot, otherwise dash
 *   press >  cancel                cancel the word
 *   gap   >  letter gap            end of letter; as soon as the word ends with TAN: access
 *   gap   >  word end              the word is dropped
 *   4 taps within 700 ms           erase: a frantic burst of taps (any length, about the
 *                                  fastest a finger goes) clears what was typed at once;
 *                                  no real letter is typed that fast
 * While the key is down the pattern callback gets the letter so far plus the element being
 * pressed: '*' while it is still a dot, '=' once it has become a dash (the "tutor").
 * Every press and every decoded letter/word is reported as a DATA notification:
 *   FD FF FF FF 'T' <elem> <press ms u16> <gap ms u16> <threshold ms u16> <speed>
 *   FD FF FF FF 'L' <letter> <pattern...>
 *   FD FF FF FF 'W' <word...>
 */
#include "morse.h"
#include "drivers/5316/clock.h"

#define GLITCH_MS     30
#define WORD_END_MIN  4000
#define SPEED_DEFAULT 9
#define SPEED_LEVELS  16
#define ERASE_TAPS    4
#define ERASE_MS      700
#define MAX_PATTERN   7
#define MAX_WORD      8

static const char *const table[] = {
    "A.-", "B-...", "C-.-.", "D-..", "E.", "F..-.", "G--.", "H....", "I..", "J.---",
    "K-.-", "L.-..", "M--", "N-.", "O---", "P.--.", "Q--.-", "R.-.", "S...", "T-",
    "U..-", "V...-", "W.--", "X-..-", "Y-.--", "Z--..",
    "0-----", "1.----", "2..---", "3...--", "4....-", "5.....", "6-....", "7--...",
    "8---..", "9----.",
    "_..--", "~.-.-", "<----", 0,
};

/* dot/dash threshold per speed level 1..16: 1000 ms down to 150 ms, each level ~12 %
 * faster (geometric). Letter gap = 2 x threshold, cancel = max(1500, 5 x threshold). */
static const u16 speed_table[SPEED_LEVELS] = {
    1000, 881, 777, 684, 603, 531, 468, 413, 364, 320, 282, 249, 219, 193, 170, 150,
};

static const morse_cb_t *cb;
static int speed = SPEED_DEFAULT;
static u32 threshold_ms, letter_gap_ms, cancel_ms;
static int live_dash;           /* the element being pressed has passed the threshold */
static int erasing;             /* this press completes an erase burst */
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

static u32 tap_tick[ERASE_TAPS];  /* start of the last presses, oldest first */
static int ntaps;

/* a new press started: is it the last of a frantic burst? */
static int burst(void) {
    int i;
    if (ntaps == ERASE_TAPS) {
        for (i = 1; i < ERASE_TAPS; i++) tap_tick[i - 1] = tap_tick[i];
        ntaps--;
    }
    tap_tick[ntaps++] = clock_time();
    return ntaps == ERASE_TAPS && ms_since(tap_tick[0]) < ERASE_MS;
}

void morse_set_speed(int level) {
    if (level < 1) level = 1;
    if (level > SPEED_LEVELS) level = SPEED_LEVELS;
    speed = level;
    threshold_ms = speed_table[level - 1];
    letter_gap_ms = 2 * threshold_ms;
    cancel_ms = 5 * threshold_ms < 1500 ? 1500 : 5 * threshold_ms;
}

int morse_speed(void) { return speed; }
u32 morse_threshold_ms(void) { return threshold_ms; }

static void show_live(int dash) {
    char disp[MAX_PATTERN + 2];
    int i;
    for (i = 0; i < npat; i++) disp[i] = pattern[i];
    disp[i++] = dash ? '=' : '*';
    disp[i] = 0;
    if (cb->pattern) cb->pattern(disp);
}

void morse_init(const morse_cb_t *c) {
    cb = c;
    morse_set_speed(speed);
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

const char *morse_code_of(char c) {
    int i;
    for (i = 0; table[i]; i++)
        if (table[i][0] == c) return table[i] + 1;
    return 0;
}


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
    if (cb->letter) cb->letter(c);
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
            if (burst()) erasing = 1;
            live_dash = 0;
            if (npat < MAX_PATTERN) show_live(0);
        } else {                       /* press ends: dt was its length */
            u8 msg[8];
            char e;
            if (dt < GLITCH_MS) {
                pressed = 0;
                if (cb->pattern) cb->pattern(pattern);
                return;
            }
            if (dt > cancel_ms) {
                morse_reset();
                e = 'X';
            } else {
                e = dt < threshold_ms ? '.' : '-';
            }
            if (e != 'X' && npat < MAX_PATTERN) {
                pattern[npat++] = e;
                pattern[npat] = 0;
            }
            if (erasing) {              /* end of the press that completed a burst */
                erasing = 0;
                ntaps = 0;
                send('W', (const u8 *)"", 0);
                morse_reset();
                if (cb->erased) cb->erased();
                e = 'C';
            }
            if (cb->pattern) cb->pattern(pattern);
            msg[0] = e;
            msg[1] = dt;
            msg[2] = dt >> 8;
            msg[3] = last_gap_ms;
            msg[4] = last_gap_ms >> 8;
            msg[5] = threshold_ms;
            msg[6] = threshold_ms >> 8;
            msg[7] = speed;
            send('T', msg, sizeof(msg));
        }
        pressed = touched;
        t_edge = clock_time();
        return;
    }

    if (pressed) {
        if (!live_dash && dt >= threshold_ms && npat < MAX_PATTERN) {
            live_dash = 1;             /* the tutor grows from dot to dash */
            show_live(1);
        }
    } else {
        u32 word_end = 3 * letter_gap_ms < WORD_END_MIN ? WORD_END_MIN : 3 * letter_gap_ms;
        if (npat && dt > letter_gap_ms) end_letter();
        if (nword && !npat && dt > word_end) {
            send('W', (const u8 *)word, nword);
            morse_reset();
        }
    }
}
