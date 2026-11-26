/*
 * MorseOS link log: how far the band can go from the PC (or the phone) it is connected to.
 *
 * There is no accelerometer driver (chip not identified yet) and no step counter, so the
 * distance is an estimate from the signal strength of the link (RSSI, averaged), with the
 * log-distance model  d = 10 ^ ((RSSI_1m - RSSI) / (10 n)):  RSSI_1m is measured holding the
 * band 1 m from the PC (calibration), n = 2 in free space, 2.5-3.5 indoors. Body, walls and
 * the way the band faces the PC easily change it by 2x: a hint, not a measure.
 * The firmer number is the last RSSI before the link drops by supervision timeout (reason
 * 0x08: out of range), kept as the range; a PC that disconnects (0x13) does not count.
 * The log (connections, losses with their reason, an RSSI sample every N s) is RAM only.
 */
#ifndef LINKLOG_H
#define LINKLOG_H

#include "tl_common.h"

#define LINKLOG_N 32

enum { LL_SAMPLE, LL_CONNECT, LL_LOST };

typedef struct {
    u16 t2;      /* uptime / 2 s */
    u8 kind;     /* LL_* */
    s8 v;        /* RSSI dBm (sample), reason (lost) */
} linklog_ev_t;

void linklog_connect(void);
void linklog_lost(u8 reason);              /* from the terminate event */
void linklog_poll(int connected, int rssi_dbm);  /* main loop */

int  linklog_rssi(void);         /* averaged RSSI of the link, dBm (-127 = none) */
int  linklog_min_rssi(void);     /* weakest averaged RSSI of this connection */
void linklog_reset_min(void);
int  linklog_range(void);        /* last averaged RSSI before an out-of-range loss, 0 = none */
u32  linklog_distance_cm(int rssi_dbm);
int  linklog_rssi_1m(void);
void linklog_set_rssi_1m(int dbm);
int  linklog_n10(void);          /* n x 10 */
void linklog_set_n10(int n10);
int  linklog_interval(void);     /* s between samples */
void linklog_set_interval(int s);
int  linklog_count(void);
const linklog_ev_t *linklog_get(int i);   /* oldest first */
void linklog_clear(void);

#endif
