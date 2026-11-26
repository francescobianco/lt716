/*
 * MorseOS heart-rate sensor: HRS3300 at I2C 0x44 (ID 0x21 in register 0x00), on a software
 * I2C: SDA = PA4, SCL = PC7 on the first band (from the stock firmware's disassembly); on the
 * second band the same pins are only a guess, so they can be changed (/health/hr_pins).
 *
 * The stock firmware (V10712, and the server images V08928 G and V17899 D, disassembled
 * 2026-11-26) shares SDA = PA4 with its hardware I2C (SCL = PA3, most likely the
 * accelerometer). A slave stopped mid-byte holds SDA low: since 0.8.9 the bus is unstuck with
 * up to 9 clock pulses on PC7, then PA3, before giving up with "lines low".
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
/* power pin of the sensor, active low ("A7"; -1 = none): on the second band an unpowered
 * sensor would explain both lines read low ("LINEE BASSE"); driven low only while in use */
void hrs_power_text(char *out);            /* "A7" or "-" */
int  hrs_set_power(const char *s);         /* "A7" or "-": 0 ok, -1 bad */
/* scan, read only (command, never on its own): with the power pin low, the level at rest of
 * every allowed pin with its pull-up, then every ordered pair of pins that are both high,
 * probing the addresses of the usual band sensors. Result as text for /health/hr_scan. */
void hrs_scan(void);
void hrs_scan_text(void (*put)(const char *s));
/* diagnostics (0.8.11), on command only: drive an allowed pin "A7 0" / "A7 1" / "A7 z"
 * (released); the list of driven pins as text. And the ADC pins by name: "B5" -> GPIO, 0 if
 * not an ADC pin (PA7, PB0..PB7). */
int  hrs_pin_cmd(const char *s);              /* 0 ok, -1 bad */
void hrs_pin_text(void (*put)(const char *s));
u32  hrs_adc_pin(const char *name);
/* resistor put on the ADC pin before each measure (0.8.16, the photodiode hunt on PB5):
 * "-" leave it, "f" none, "u1m" 1M pull-up, "u10k" 10k pull-up, "d100k" 100k pull-down */
int  hrs_adc_pull_set(const char *s);   /* 0 ok, -1 bad */
const char *hrs_adc_pull_name(void);
void hrs_adc_pull_apply(u32 pin);
/* pins as "A4 C7": the names of the allowed pins */
void hrs_pins_text(char *out);
int  hrs_set_pins(const char *s);  /* 0 ok, -1 bad or not allowed */

#endif
