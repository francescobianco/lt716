/* MorseOS: adaptive Morse decoder for the touch key. No drawing: events go to callbacks. */
#ifndef MORSE_H
#define MORSE_H

#include "tl_common.h"

#define MORSE_ACCESS_WORD "TAN" /* - .- -. */

/* special codes, returned as these characters */
#define MORSE_SPACE     '_'  /* ..--  */
#define MORSE_ENTER     '~'  /* .-.-  */
#define MORSE_BACKSPACE '<'  /* ----  */

typedef struct {
    void (*notify)(const u8 *pkt, int len); /* 20-byte DATA notification (touchlog) */
    void (*pattern)(const char *pat);       /* letter being typed; '*' / '=' = element being
                                               pressed, still a dot / already a dash */
    void (*word)(const char *word);         /* letters so far ("" = cleared) */
    void (*access)(void);                   /* the word ends with MORSE_ACCESS_WORD */
    void (*letter)(char c);                 /* every decoded letter, incl. the specials */
    void (*erased)(void);                   /* a burst of 6 dots cleared the input */
} morse_cb_t;

void morse_init(const morse_cb_t *cb);
void morse_reset(void);       /* forget the current letter and word */
void morse_poll(int touched); /* call on every main-loop pass with the key level */
const char *morse_code_of(char c); /* ".-" for A, 0 if unknown */
void morse_set_speed(int level);  /* 1 = very slow .. 16 = very fast, default 9 */
int  morse_speed(void);
u32  morse_threshold_ms(void);    /* presses shorter than this are dots */

#endif
