/*
 * MorseOS heart-rate sensor: HRS3300 at I2C 0x44 (ID 0x21 in register 0x00), on a software
 * I2C: SDA = PA4, SCL = PC7 on the first band (from the stock firmware's disassembly); on the
 * second band the same pins are only a guess, so they can be changed (/health/hr_pins).
 *
 * Safety (rules after the 0.5.5 incident): nothing is touched until hrs_start(), called when
 * HEALTH > BATTITO opens; both lines must read high at rest, the ID must be 0x21, and only
 * then the registers are written (the stock firmware's values). Lines are only pulled low or
 * released (internal 10k pull-ups), never driven high. The sensor is switched off and the
 * pins released when nobody asks for it for 1 s (hrs_keep, hrs_poll).
 *
 * Channels (16/18-bit ADC counts, the InfiniTime driver's bit layout):
 *   C0 = the green LED's light reflected by the wrist + ambient light
 *   C1 = ambient light only
 */
#ifndef HRS3300_H
#define HRS3300_H

#include "tl_common.h"

enum { HRS_OFF, HRS_ON, HRS_ERR_LINES, HRS_ERR_NOACK, HRS_ERR_ID };

int  hrs_start(void);              /* HRS_ON or an error */
int  hrs_state(void);
u8   hrs_id(void);                 /* ID read by the last start */
void hrs_keep(void);               /* keep it on for 1 s more */
void hrs_poll(void);               /* main loop: off when not kept */
int  hrs_read(u32 *c0, u32 *c1);   /* 1 = read */
/* pins as "A4 C7": the names of the allowed pins */
void hrs_pins_text(char *out);
int  hrs_set_pins(const char *s);  /* 0 ok, -1 bad or not allowed */

#endif
