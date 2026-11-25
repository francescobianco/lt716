/* MorseOS battery and charger sense (LT716: PB1 = VBAT/4, PB2 = charger rail/8). */
#ifndef BATTERY_H
#define BATTERY_H

#include "tl_common.h"

void battery_update(void);   /* sample both channels (a few ms) */
void battery_poll(void);     /* from the main loop: battery_update() every 30 s */
u32  battery_mv(void);       /* last battery reading */
int  battery_level(void);    /* 0..100 from a Li-ion curve, -120 mV while charging */
int  battery_pct(u32 mv, int charging, int full);  /* the same for any reading */
int  battery_charging(void); /* charger rail present */
int  battery_full(void);     /* on the charger and the charge is complete */

#endif
