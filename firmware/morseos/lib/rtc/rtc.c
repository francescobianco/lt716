/* Seconds since 2000-01-01 00:00 (a Saturday), advanced from the system tick. */
#include "rtc.h"
#include "drivers/5316/clock.h"

#define TICKS_PER_SEC (sys_tick_per_us * 1000000u)

static u32 secs;        /* local time, seconds since 2000-01-01 */
static u32 up;          /* seconds since boot */
static u32 last_tick;
static u32 frac;        /* ticks not yet turned into a second */
static int is_set;

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int mdays(int y, int m) {
    static const u8 d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leap(y) ? 29 : d[m - 1];
}

void rtc_poll(void) {
    u32 now = clock_time();
    frac += now - last_tick;
    last_tick = now;
    while (frac >= TICKS_PER_SEC) {
        frac -= TICKS_PER_SEC;
        secs++;
        up++;
    }
}

void rtc_set(int year, int month, int day, int hour, int min, int sec) {
    u32 days = 0;
    int y, m;
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return;
    for (y = 2000; y < year; y++) days += leap(y) ? 366 : 365;
    for (m = 1; m < month; m++) days += mdays(year, m);
    days += day - 1;
    rtc_poll();
    secs = days * 86400 + hour * 3600 + min * 60 + sec;
    frac = 0;
    is_set = 1;
}

void rtc_get(rtc_time_t *t) {
    u32 s = secs, days = s / 86400;
    int y = 2000, m = 1;
    s %= 86400;
    t->hour = s / 3600;
    t->min = s / 60 % 60;
    t->sec = s % 60;
    t->wday = (days + 6) % 7; /* 2000-01-01 was a Saturday (6) */
    while (days >= (u32)(leap(y) ? 366 : 365)) days -= leap(y++) ? 366 : 365;
    while (days >= (u32)mdays(y, m)) days -= mdays(y, m++);
    t->year = y;
    t->month = m;
    t->day = days + 1;
}

int rtc_is_set(void) { return is_set; }
u32 rtc_uptime(void) { return up; }
