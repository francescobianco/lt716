/*
 * MorseOS hardware discovery (the "diag" bundle, diag.ini): what hangs on every pin.
 *   1. profile: each pin read with its 100k pull-down and with its 10k pull-up:
 *        f floating (follows the pull)   H held high   L held low   ? reads the opposite
 *   2. I2C: on every ordered pair of pins that can be high at rest, every 7-bit address
 *      0x08..0x77; for each device that answers, registers 0x00, 0x01 and 0x0F (the ID
 *      registers of the usual band sensors)
 *   3. (v2, second band) SPI: MOSI = PC3, SCK = PC5 (the display's lines, display deselected
 *      with its CS PA1 high), mode 3; every free pin tried as CS and every pin (PC3 too, for
 *      3-wire) as MISO; registers 0x0F and 0x00 read (0x8F / 0x80)
 * Pins already known are left alone: touch key PC2, motor PA5, battery/charger sense PB1/PB2
 * and the display pins of the band found at boot (on the second band, v2: only its power PA0,
 * CS PA1, backlight PA2 and reset PA6; PC3, PC5 and the DC PC6 are scanned and the display is
 * initialised again at the end). I2C lines are only pulled low or released.
 * Runs a pair per main-loop pass (no watchdog trouble); on command only (/hw/scan).
 */
#ifndef HWSCAN_H
#define HWSCAN_H

void hwscan_start(void);
void hwscan_poll(void);   /* main loop */
int  hwscan_busy(void);
void hwscan_text(void (*put)(const char *s));

#endif
