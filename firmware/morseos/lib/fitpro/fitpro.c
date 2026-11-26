/*
 * FitPro packets: CD <len u16 BE = total-3> <group> 01 <cmd> <datalen u16 BE> <data>,
 * written in 20-byte pieces and reassembled by the length field. Every complete packet
 * is acknowledged like the stock firmware does: DC 00 05 <group> <cmd> 00 09 <01 = ok>.
 */
#include "fitpro.h"
#include "rtc.h"

/* only the start of a packet is kept: header (8) + notification type (3) + the text the message
 * screen shows (120); longer packets are still counted to the end and acknowledged */
#define BUF_MAX (8 + 3 + 120 + 1)

static const fitpro_cb_t *cb;
static u8 buf[BUF_MAX];
static int nbuf, nrecv;  /* bytes kept, bytes received */
static volatile int ready;

void fitpro_init(const fitpro_cb_t *c) { cb = c; }

int fitpro_on_write(rf_packet_att_write_t *p) {
    int len = p->l2capLen - 3, i;
    u8 *d = &p->value;
    if (len <= 0 || ready) return 0;
    if (d[0] == 0xCD) nbuf = nrecv = 0;
    for (i = 0; i < len && nbuf < BUF_MAX; i++) buf[nbuf++] = d[i];
    nrecv += len;
    if (nbuf >= 3 && buf[0] == 0xCD && nrecv >= 3 + (buf[1] << 8 | buf[2])) ready = 1;
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
    } else if (group == 0x12 && cmd == 0x02) {                /* alarms: the whole list */
        if (cb->alarms_set) cb->alarms_set(data, dlen);
        ok = 1;
    } else if (group == 0x15 && cmd == 0x0D && cb->steps) {   /* today's totals */
        /* CD 00 0F 15 01 0C 00 0C <00 00> <steps u32> <distance m u32> <kcal u16>, as the stock
         * firmware (0x2658 in V10712); distance 70 cm and 0.04 kcal per step, estimates */
        u8 r[20] = {0xCD, 0x00, 0x0F, 0x15, 0x01, 0x0C, 0x00, 0x0C};
        u32 st = cb->steps(), m = st * 70 / 100, kc = st * 4 / 100;
        r[10] = st >> 24; r[11] = st >> 16; r[12] = st >> 8; r[13] = st;
        r[14] = m >> 24; r[15] = m >> 16; r[16] = m >> 8; r[17] = m;
        r[18] = kc >> 8; r[19] = kc;
        if (cb->tx) cb->tx(r, 20);
        ok = 1;
    } else if (group == 0x18) {                               /* read the alarms */
        /* CD <len u16 = total-3> 18 01 10 <datalen u16> <5 bytes per alarm>, in 20-byte pieces */
        u8 r[8 + 8 * 5];
        int dl = cb->alarms_get ? cb->alarms_get(r + 8) : 0, i, tot = 8 + dl;
        r[0] = 0xCD;
        r[1] = (tot - 3) >> 8;
        r[2] = tot - 3;
        r[3] = 0x18;
        r[4] = 0x01;
        r[5] = 0x10;
        r[6] = dl >> 8;
        r[7] = dl;
        for (i = 0; i < tot && cb->tx; i += 20) cb->tx(r + i, tot - i < 20 ? tot - i : 20);
        nbuf = nrecv = 0;
        ready = 0;
        return;                                               /* the reply is the answer */
    } else if (group == 0x12 && cmd == 0x0B) {                /* find band */
        if (cb->find) cb->find();
        ok = 1;
    } else if (group == 0x12 && (cmd == 0x12 || cmd == 0x11) && dlen >= 2) {
        /* 12 12: <type> 00 00 <text>; 12 11: <state> 00 <caller>, state 0 = call ended */
        int skip = cmd == 0x12 ? 3 : 2;
        char *text = (char *)buf;  /* in place: the text moves down to the start of buf */
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
    nbuf = nrecv = 0;
    ready = 0;
}
