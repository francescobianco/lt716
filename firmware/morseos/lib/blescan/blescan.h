/*
 * MorseOS Bluetooth scan: the devices advertising nearby, while the band stays a peripheral
 * (the Telink library scans between its own advertising events and in the gaps of a
 * connection, like the SDK sample 5316_feature_test TEST_SCAN_IN_ADV_AND_CONN_SLAVE_ROLE).
 *
 * Off by default and nothing changes at boot: the scan is set up the first time it is asked
 * for and runs only while someone keeps asking (blescan_request), then it is taken down again
 * (scan flags off, HCI handler and event mask cleared). The CPU does not suspend while it runs.
 * The reports arrive in the radio interrupt: the handler only copies a few bytes.
 */
#ifndef BLESCAN_H
#define BLESCAN_H

#include "tl_common.h"

#define BLESCAN_MAX  6   /* 0.8.8: was 8 (RAM) */
#define BLESCAN_NAME 8

typedef struct {
    u8 mac[6];             /* little endian, as on the air */
    u8 addr_type;          /* 0 public, 1 random */
    s8 rssi;               /* dBm */
    char name[BLESCAN_NAME + 1];  /* from the advertising data, "" if none */
    u32 seen;              /* clock_time() of the last report */
} blescan_dev_t;

void blescan_request(u32 ms);  /* keep scanning for at least ms from now */
void blescan_stop(void);
void blescan_poll(void);       /* main loop: start / stop */
int  blescan_active(void);
void blescan_clear(void);
/* the devices strongest first: fills idx, returns how many */
int  blescan_sorted(u8 idx[BLESCAN_MAX]);
const blescan_dev_t *blescan_dev(int i);
u32  blescan_age_s(const blescan_dev_t *d);

#endif
