/*
 * The green LED under the band. Second band only: PC7, active low (found 2026-11-26). On the
 * first band PC7 is the HRS3300's I2C clock and the LED belongs to that chip: nothing here.
 * The LED stays on only while it is asked for (greenled_keep every pass): greenled_poll() in
 * the main loop switches it off and releases the pin 300 ms after the last request, so an app
 * replaced by an alarm, a notification or the emergency screen cannot leave it on.
 */
#ifndef GREENLED_H
#define GREENLED_H

int  greenled_available(void);   /* 1 on the second band */
void greenled_set(int on);       /* also keeps it */
void greenled_poll(void);        /* main loop */

#endif
