/*
 * MorseOS sensor finder: software I2C (open drain, internal 10k pull-ups, read only) over
 * every ordered pair of PA3, PA4, PC4 and PC7 (the SWS pad, used by the stock firmware's
 * software I2C as SCL with SDA on PA4), recovering a stuck bus first, and listing the
 * devices that answer and reading the ID register of the usual band sensors.
 */
#ifndef I2CPROBE_H
#define I2CPROBE_H

#include "tl_common.h"

#define I2CPROBE_MAX 16

typedef struct {
    u8 sda, scl;     /* index in i2cprobe_pin_name() */
    u8 addr;         /* 7-bit address that answered */
    u8 id_reg, id;   /* ID register read and its value (id_reg 0xFF = not read) */
} i2cprobe_hit_t;

typedef struct {
    int n;                       /* devices found */
    i2cprobe_hit_t hit[I2CPROBE_MAX];
    u8 busy_mask;                /* bit i = pin i was low at rest (pair skipped) */
} i2cprobe_result_t;

void i2cprobe_run(i2cprobe_result_t *r);
const char *i2cprobe_pin_name(int i);       /* "A3", "A4", "C4" */
const char *i2cprobe_guess(const i2cprobe_hit_t *h);  /* "HRS3300", "SC7A20", ... or "?" */

#endif
