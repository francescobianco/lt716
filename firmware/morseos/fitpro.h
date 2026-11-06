/* MorseOS: the subset of the FitPro UART protocol it understands (see files/protocol-notes.md). */
#ifndef FITPRO_H
#define FITPRO_H

#include "tl_common.h"
#include "stack/ble/ble_common.h"

typedef struct {
    void (*tx)(const u8 *p, int len);   /* notify on 6e400003 */
    void (*find)(void);                 /* 12 0B: find band */
    void (*message)(const char *text);  /* 12 12 / 12 11: notification or call, UTF-8 */
} fitpro_cb_t;

void fitpro_init(const fitpro_cb_t *cb);
int  fitpro_on_write(rf_packet_att_write_t *p);  /* ATT write callback of 6e400002 */
void fitpro_poll(void);                         /* handle a complete packet, from the main loop */

#endif
