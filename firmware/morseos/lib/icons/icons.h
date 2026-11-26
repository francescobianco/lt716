/* MorseOS menu icons, 32 x 32 px, 4-bit antialiased, run-length coded (tools/mkassets.py). */
#ifndef ICONS_H
#define ICONS_H

#include "tl_common.h"

#define ICON_SIZE 32

const u8 *icon_for(const char *label); /* icon of a menu entry by its label, 0 = none */
const u8 *icon_back(void);             /* back arrow (TORNA / INDIETRO) */
void icon_draw(int x, int y, const u8 *icon, u16 fg, u16 bg);
void icon_bell(int x, int y, u16 fg, u16 bg);  /* 10 x 10: alarms are set (watch face) */

#endif
