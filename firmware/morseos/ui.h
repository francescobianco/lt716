/* MorseOS user interface: watch face, square-box menu, info and message screens. */
#ifndef UI_H
#define UI_H

#include "tl_common.h"

#define MORSEOS_VERSION "0.3"

typedef struct {
    void (*notify)(const u8 *pkt, int len); /* touchlog notifications */
    void (*buzz)(int ms);
} ui_cb_t;

void ui_init(const ui_cb_t *cb);
void ui_poll(int touched);          /* every main-loop pass */
void ui_set_connected(int on);
void ui_redraw(void);               /* e.g. after the LCD geometry changed */
void ui_find(void);                 /* "find band" from the phone/PC */
void ui_message(const char *text);  /* notification text */
u32  ui_press_ms(void);             /* current press length, 0 = released */
int  ui_battery_level(void);

#endif
