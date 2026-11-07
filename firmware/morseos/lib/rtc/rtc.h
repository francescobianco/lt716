/* MorseOS software clock on the 16 MHz system tick (call rtc_poll() at least every 4 min). */
#ifndef RTC_H
#define RTC_H

#include "tl_common.h"

typedef struct {
    int year, month, day, hour, min, sec;
    int wday; /* 0 = Sunday */
} rtc_time_t;

void rtc_poll(void);
void rtc_set(int year, int month, int day, int hour, int min, int sec);
void rtc_get(rtc_time_t *t);
int  rtc_is_set(void);
u32  rtc_uptime(void); /* seconds since boot */

#endif
