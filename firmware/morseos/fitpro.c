/*
 * FitPro packets: CD <len u16 BE = total-3> <group> 01 <cmd> <datalen u16 BE> <data>,
 * written in 20-byte pieces and reassembled by the length field. Every complete packet
 * is acknowledged like the stock firmware does: DC 00 05 <group> <cmd> 00 09 <01 = ok>.
 */
#include "fitpro.h"
#include "rtc.h"

#define BUF_MAX 340

static const fitpro_cb_t *cb;
static u8 buf[BUF_MAX];
static int nbuf;
static volatile int ready;

void fitpro_init(const fitpro_cb_t *c) { cb = c; }

int fitpro_on_write(rf_packet_att_write_t *p) {
    int len = p->l2capLen - 3, i;
    u8 *d = &p->value;
    if (len <= 0 || ready) return 0;
    if (d[0] == 0xCD) nbuf = 0;
    for (i = 0; i < len && nbuf < BUF_MAX; i++) buf[nbuf++] = d[i];
    if (nbuf >= 3 && buf[0] == 0xCD && nbuf >= 3 + (buf[1] << 8 | buf[2])) ready = 1;
    return 0;
}

static void ack(u8 group, u8 cmd, u8 ok) {
    u8 a[8] = {0xDC, 0x00, 0x05, group, cmd, 0x00, 0x09, ok};
    if (cb->tx) cb->tx(a, sizeof(a));
}

void fitpro_poll(void) {
    u8 group, cmd, *data;
    int dlen, ok = 0;
    if (!ready) return;
    group = buf[3];
    cmd = buf[5];
    dlen = nbuf >= 8 ? (buf[6] << 8 | buf[7]) : 0;
    data = buf + 8;
    if (8 + dlen > nbuf) dlen = nbuf - 8;

    if (group == 0x12 && cmd == 0x01 && dlen >= 4) {          /* set time */
        u32 v = (u32)data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
        rtc_set(2000 + (v >> 26), v >> 22 & 0xF, v >> 17 & 0x1F, v >> 12 & 0x1F, v >> 6 & 0x3F,
                v & 0x3F);
        ok = 1;
    } else if (group == 0x12 && cmd == 0x0B) {                /* find band */
        if (cb->find) cb->find();
        ok = 1;
    } else if (group == 0x12 && (cmd == 0x12 || cmd == 0x11) && dlen >= 2) {
        /* 12 12: <type> 00 00 <text>; 12 11: <state> 00 <caller>, state 0 = call ended */
        int skip = cmd == 0x12 ? 3 : 2;
        static char text[BUF_MAX];
        int i, n = 0;
        if (!(cmd == 0x11 && data[0] == 0)) {
            for (i = skip; i < dlen && n < BUF_MAX - 1; i++) text[n++] = data[i];
            text[n] = 0;
            if (cb->message) cb->message(text);
        }
        ok = 1;
    } else if (group == 0x12) {
        ok = 1;  /* other settings: accepted and ignored for now */
    }
    ack(group, cmd, ok);
    nbuf = 0;
    ready = 0;
}
