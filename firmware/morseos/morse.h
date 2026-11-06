/* MorseOS: adaptive Morse decoder for the touch key. No drawing: events go to callbacks. */
#ifndef MORSE_H
#define MORSE_H

#include "tl_common.h"

#define MORSE_ACCESS_WORD "TAN" /* - .- -. */

typedef struct {
    void (*notify)(const u8 *pkt, int len); /* 20-byte DATA notification (touchlog) */
    void (*pattern)(const char *pat);       /* elements of the letter being typed */
    void (*word)(const char *word);         /* letters so far ("" = cleared) */
    void (*access)(void);                   /* the word ends with MORSE_ACCESS_WORD */
} morse_cb_t;

void morse_init(const morse_cb_t *cb);
void morse_reset(void);       /* forget the current letter and word */
void morse_poll(int touched); /* call on every main-loop pass with the key level */
u32  morse_dot_ms(void);
u32  morse_dash_ms(void);

#endif
