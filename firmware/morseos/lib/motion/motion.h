/*
 * MorseOS motion sensor and step counter. The step sensor of these bands is a vibration switch
 * on PA4, not an accelerometer (found 2026-11-26 in the stock firmware V10712: PA4 interrupt
 * with the polarity flipped at every edge, fed to its step library); closed to ground at rest,
 * it bounces ~100 times a second when shaken.
 *
 *   edges   PA4 changes, read in the main loop; PA4 also wakes the CPU from suspend, with the
 *           wake-up level always the opposite of the current one (motion_wake_level), so the
 *           band wakes once per change and never stays awake on a switch left open
 *   hits    an edge after at least `quiet` ms without edges starts a hit (the bounces of one
 *           jolt are one hit)
 *   steps   a hit `min`..`max` ms after the previous one is in cadence; after `confirm` hits
 *           in cadence in a row they all count, then every further hit in cadence counts. A
 *           pause longer than `max` starts over, so isolated jolts never count
 * The total is today's (zeroed when the date changes), RAM only. Distance and calories are
 * estimates: 70 cm and 0.04 kcal per step.
 */
#ifndef MOTION_H
#define MOTION_H

#include "tl_common.h"

void motion_init(void);
void motion_poll(void);        /* main loop */
int  motion_level(void);
int  motion_wake_level(void);  /* the level that must wake the CPU: the opposite of now */
u32  motion_edges(void);
void motion_clear(void);
u32  motion_hits(void);
u32  steps_today(void);
void steps_reset(void);
u32  steps_distance_m(void);
u32  steps_kcal(void);
/* parameters "quiet min max confirm" (ms, ms, ms, hits) */
void steps_cfg_text(char *out);   /* at least 24 bytes */
int  steps_set_cfg(const char *s);

#endif
