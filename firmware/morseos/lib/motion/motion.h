/*
 * MorseOS motion sensor: on these bands the step counter is not an accelerometer but a
 * vibration switch on PA4 (found 2026-11-26 in the stock firmware V10712: PA4 interrupt with
 * the polarity flipped at every edge, the edges fed to the step library, the steps added to the
 * total that FitPro 15 0D reports). At rest it holds PA4 low (closed to ground).
 * This first version only polls PA4 from the main loop and counts its edges (edges are missed
 * while the CPU sleeps): enough to see it react, before a step counter is built on it.
 */
#ifndef MOTION_H
#define MOTION_H

#include "tl_common.h"

void motion_init(void);
void motion_poll(void);       /* main loop */
int  motion_level(void);
u32  motion_edges(void);
void motion_clear(void);

#endif
