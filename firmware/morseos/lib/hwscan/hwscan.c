/* MorseOS hardware discovery, see hwscan.h. */
#include "hwscan.h"
#include "st7735.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

#define NPIN    24
#define MAXHIT  16
#define HALF_US 10

static u8 prof[NPIN];        /* 0 skipped, 'f', 'H', 'L', '?' */
static u8 cand[NPIN], ncand;
static u8 hit[MAXHIT][6];    /* sda, scl, addr, reg00, reg01, reg0f */
static u8 nhit, busy, done;
static int pi, pj;           /* next pair */
static u32 sda, scl;

static u32 pin_of(int i) { return (u32)(i / 8) << 8 | 1u << (i % 8); }

static void pin_name(int i, char *t) {
    t[0] = 'A' + i / 8;
    t[1] = '0' + i % 8;
}

static int skipped(int i) {
    static const u8 common[] = {2 * 8 + 2, 0 * 8 + 5, 1 * 8 + 1, 1 * 8 + 2};        /* C2 A5 B1 B2 */
    static const u8 band1[] = {0 * 8 + 1, 0 * 8 + 6, 1 * 8 + 3, 2 * 8 + 1, 2 * 8 + 3, 2 * 8 + 5};
    static const u8 band2[] = {0 * 8 + 0, 0 * 8 + 1, 0 * 8 + 2, 0 * 8 + 6, 2 * 8 + 3, 2 * 8 + 5,
                               2 * 8 + 6};
    unsigned k;
    const u8 *b = lcd_board && lcd_board->band == 2 ? band2 : band1;
    unsigned nb = lcd_board && lcd_board->band == 2 ? sizeof(band2) : sizeof(band1);
    for (k = 0; k < sizeof(common); k++)
        if (common[k] == i) return 1;
    for (k = 0; k < nb; k++)
        if (b[k] == i) return 1;
    return 0;
}

static void as_input(u32 p, int pull) {
    gpio_set_func(p, AS_GPIO);
    gpio_write(p, 0);
    gpio_set_output_en(p, 0);
    gpio_set_input_en(p, 1);
    gpio_setup_up_down_resistor(p, pull);
}

static void release(u32 p) { gpio_set_output_en(p, 0); }
static void low(u32 p) {
    gpio_write(p, 0);
    gpio_set_output_en(p, 1);
}
static void dly(void) { sleep_us(HALF_US); }

static void start(void) {
    release(sda); release(scl); dly();
    low(sda); dly();
    low(scl); dly();
}

static void stop(void) {
    low(sda); dly();
    release(scl); dly();
    release(sda); dly();
}

static int put(u8 b) {
    int i, ack;
    for (i = 7; i >= 0; i--) {
        if (b >> i & 1) release(sda); else low(sda);
        dly();
        release(scl); dly();
        low(scl);
    }
    release(sda); dly();
    release(scl); dly();
    ack = !gpio_read(sda);
    low(scl); dly();
    return ack;
}

static u8 get(void) {
    int i;
    u8 b = 0;
    release(sda);
    for (i = 0; i < 8; i++) {
        dly();
        release(scl); dly();
        b = b << 1 | (gpio_read(sda) ? 1 : 0);
        low(scl);
    }
    release(sda); dly();
    release(scl); dly();
    low(scl); dly();
    return b;
}

static u8 rd(u8 addr, u8 reg) {
    u8 v = 0xFF;
    start();
    if (put(addr << 1) && put(reg)) {
        start();
        if (put(addr << 1 | 1)) v = get();
    }
    stop();
    return v;
}

void hwscan_start(void) {
    int i;
    if (busy) return;
    nhit = ncand = 0;
    done = 0;
    for (i = 0; i < NPIN; i++) {
        u32 p = pin_of(i);
        int d, u;
        prof[i] = 0;
        if (skipped(i)) continue;
        as_input(p, PM_PIN_PULLDOWN_100K);
        sleep_us(300);
        d = gpio_read(p) ? 1 : 0;
        as_input(p, PM_PIN_PULLUP_10K);
        sleep_us(300);
        u = gpio_read(p) ? 1 : 0;
        prof[i] = u && !d ? 'f' : u && d ? 'H' : !u && !d ? 'L' : '?';
        if (u) cand[ncand++] = i;                 /* can be high at rest: an I2C line? */
        else as_input(p, PM_PIN_UP_DOWN_FLOAT);
    }
    pi = 0;
    pj = 1;
    busy = ncand >= 2;
    if (!busy) {
        for (i = 0; i < ncand; i++) as_input(pin_of(cand[i]), PM_PIN_UP_DOWN_FLOAT);
        done = 1;
    }
}

/* one ordered pair per call: all addresses */
void hwscan_poll(void) {
    int a, i;
    if (!busy) return;
    if (pi != pj) {
        sda = pin_of(cand[pi]);
        scl = pin_of(cand[pj]);
        for (a = 0x08; a < 0x78 && nhit < MAXHIT; a++) {
            int ack;
            start();
            ack = put(a << 1);
            stop();
            if (ack) {
                u8 *h = hit[nhit++];
                h[0] = cand[pi];
                h[1] = cand[pj];
                h[2] = a;
                h[3] = rd(a, 0x00);
                h[4] = rd(a, 0x01);
                h[5] = rd(a, 0x0F);
            }
        }
    }
    if (++pj == ncand) {
        pj = 0;
        if (++pi == ncand) {                      /* all pairs done: pins back to float */
            for (i = 0; i < ncand; i++) as_input(pin_of(cand[i]), PM_PIN_UP_DOWN_FLOAT);
            busy = 0;
            done = 1;
        }
    }
}

int hwscan_busy(void) { return busy; }

static void hex2(u8 v, char *t) {
    t[0] = "0123456789abcdef"[v >> 4];
    t[1] = "0123456789abcdef"[v & 15];
}

void hwscan_text(void (*put_s)(const char *s)) {
    int i;
    char t[24];
    if (busy) {
        put_s("running\n");
        return;
    }
    if (!done) {
        put_s("not run: write 1\n");
        return;
    }
    put_s("pins (f float, H held high, L held low, - skipped)\n");
    for (i = 0; i < NPIN; i++) {
        pin_name(i, t);
        t[2] = '=';
        t[3] = prof[i] ? prof[i] : '-';
        t[4] = (i % 8 == 7) ? '\n' : ' ';
        t[5] = 0;
        put_s(t);
    }
    put_s("i2c: sda scl addr r00 r01 r0f\n");
    for (i = 0; i < nhit; i++) {
        pin_name(hit[i][0], t);
        t[2] = ' ';
        pin_name(hit[i][1], t + 3);
        t[5] = ' ';
        hex2(hit[i][2], t + 6);
        t[8] = ' ';
        hex2(hit[i][3], t + 9);
        t[11] = ' ';
        hex2(hit[i][4], t + 12);
        t[14] = ' ';
        hex2(hit[i][5], t + 15);
        t[17] = '\n';
        t[18] = 0;
        put_s(t);
    }
    if (!nhit) put_s("no device\n");
}
