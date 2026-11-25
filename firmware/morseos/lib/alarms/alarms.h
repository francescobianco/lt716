/*
 * MorseOS alarms: up to 8, each a time and the days of the week. Set from the PC with
 * ./lt716 alarms (FitPro 12 02, read back with 18) or the file /clock/alarms. RAM only for
 * now: lost on reboot, and they ring only once the clock is set.
 */
#ifndef ALARMS_H
#define ALARMS_H

#include "tl_common.h"

#define ALARMS_MAX   8
#define ALARMS_DAILY 0x7F

typedef struct {
    u8 hour, min;
    u8 days;  /* bit 0 = Monday .. bit 6 = Sunday; 0 = once (removed after it rings) */
} alarm_t;

int  alarms_count(void);
const alarm_t *alarms_get(int i);  /* sorted by time */
void alarms_clear(void);
int  alarms_set(int hour, int min, int days);  /* add, or change the days of that time; -1 full */
int  alarms_del(int hour, int min);            /* -1 not found */
/* one line of /clock/alarms: "HH:MM [MON..SUN|DAILY|ONCE]..." adds/changes, "-HH:MM" removes,
 * "" clears all; 0 ok, -1 bad line, -2 full */
int  alarms_line(const char *s);
/* "MON TUE", "DAILY", "ONCE" into out (at least 28 bytes) */
void alarms_days_text(int days, char *out);
/* FitPro 12 02 data: 5 bytes per alarm, 40-bit BE ...hour<<20 | min<<14 | on<<11 | days */
void alarms_from_fitpro(const u8 *d, int len);
int  alarms_to_fitpro(u8 *out);                /* ALARMS_MAX * 5 bytes; returns the length */
int  alarms_due(void);  /* main loop: 1 once when an alarm's minute begins */

#endif
