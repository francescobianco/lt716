/* MorseOS battery and charger sense (LT716: PB1 = VBAT/4, PB2 = charger rail/8). */
#ifndef BATTERY_H
#define BATTERY_H

#include "tl_common.h"

void battery_update(void);   /* sample both channels (a few ms) */
void battery_poll(void);     /* from the main loop: battery_update() every 30 s */
u32  battery_mv(void);       /* last battery reading */
int  battery_level(void);    /* 0, 25, 50, 75, 100 like the stock gauge */
int  battery_charging(void); /* charger rail present */

#endif
