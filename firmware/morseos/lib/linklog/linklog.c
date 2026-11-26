/* MorseOS link log, see linklog.h. */
#include "linklog.h"
#include "rtc.h"

static linklog_ev_t ev[LINKLOG_N];
static u8 n_ev, head;
static int ema8 = -127 * 8, have, min_rssi = 0, range_dbm, last_raw = -127;
static int rssi_1m = -60, n10 = 25, interval = 10;
static u32 next_sample;

static void add(int kind, int v) {
    linklog_ev_t *e = &ev[head];
    e->t2 = rtc_uptime() / 2;
    e->kind = kind;
    e->v = v;
    head = (head + 1) % LINKLOG_N;
    if (n_ev < LINKLOG_N) n_ev++;
}

void linklog_connect(void) {
    have = 0;
    min_rssi = 0;
    add(LL_CONNECT, 0);
}

void linklog_lost(u8 reason) {
    if (reason == 0x08 && have) range_dbm = ema8 / 8;   /* supervision timeout: out of range */
    add(LL_LOST, reason);
    have = 0;
}

/* the RSSI is refreshed once a second by the main loop: average it (1/8 of each new reading) */
void linklog_poll(int connected, int rssi) {
    u32 up = rtc_uptime();
    if (!connected) return;
    if (rssi != last_raw && rssi > -127) {
        last_raw = rssi;
        if (!have) ema8 = rssi * 8, have = 1;
        else ema8 += rssi - ema8 / 8;
        if (!min_rssi || ema8 / 8 < min_rssi) min_rssi = ema8 / 8;
    }
    if (have && interval && up >= next_sample) {
        next_sample = up + interval;
        add(LL_SAMPLE, ema8 / 8);
    }
}

int linklog_rssi(void) { return have ? ema8 / 8 : -127; }
int linklog_min_rssi(void) { return min_rssi; }
void linklog_reset_min(void) { min_rssi = have ? ema8 / 8 : 0; }
int linklog_range(void) { return range_dbm; }
int linklog_rssi_1m(void) { return rssi_1m; }
void linklog_set_rssi_1m(int dbm) { rssi_1m = dbm; }
int linklog_n10(void) { return n10; }
void linklog_set_n10(int v) { n10 = v; }
int linklog_interval(void) { return interval; }
void linklog_set_interval(int s) { interval = s; next_sample = 0; }
int linklog_count(void) { return n_ev; }
const linklog_ev_t *linklog_get(int i) { return &ev[(head - n_ev + i + LINKLOG_N) % LINKLOG_N]; }
void linklog_clear(void) { n_ev = head = 0; range_dbm = 0; }

/* 10 ^ (k / 20) x 1000, k = 0..20: the fraction of a decade in steps of 0.05 */
static const u16 pow10_20[21] = {
    1000, 1122, 1259, 1413, 1585, 1778, 1995, 2239, 2512, 2818, 3162,
    3548, 3981, 4467, 5012, 5623, 6310, 7079, 7943, 8913, 10000,
};

u32 linklog_distance_cm(int rssi) {
    /* e = (RSSI_1m - RSSI) / (10 n) decades, in 1/1000: (rssi_1m - rssi) * 1000 / n10 / 1 */
    int e = (rssi_1m - rssi) * 1000 / n10, dec, frac, k;
    u32 d = 100;                                    /* 1 m in cm */
    if (e < -2000) e = -2000;                       /* 1 cm .. */
    if (e > 3000) e = 3000;                         /* .. 1 km: clamp (u32 below) */
    dec = e >= 0 ? e / 1000 : -((-e + 999) / 1000);
    frac = e - dec * 1000;                          /* 0..999 */
    for (; dec > 0; dec--) d *= 10;
    k = frac / 50;                                  /* interpolate between table steps */
    d = d * (pow10_20[k] + (u32)(pow10_20[k + 1] - pow10_20[k]) * (frac % 50) / 50) / 1000;
    for (; dec < 0; dec++) d /= 10;
    return d;
}
