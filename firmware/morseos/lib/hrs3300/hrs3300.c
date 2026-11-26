/* MorseOS HRS3300 driver, see hrs3300.h. Bit-banged I2C as lib/i2cprobe (~50 kHz). */
#include "hrs3300.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

#define ADDR    0x44
#define HALF_US 10

/* pins that may be tried: not the display, motor, touch key, battery ADC or SPI ones */
static const u32 allowed[] = {GPIO_PA3, GPIO_PA4, GPIO_PA7, GPIO_PB4, GPIO_PB5, GPIO_PB6,
                              GPIO_PB7, GPIO_PC0, GPIO_PC4, GPIO_PC7};
static const char *const allowed_name[] = {"A3", "A4", "A7", "B4", "B5", "B6", "B7", "C0",
                                           "C4", "C7"};
#define N_ALLOWED 10

static u8 sda_i = 1, scl_i = 9;  /* A4, C7 */
static s8 pwr_i = -1;            /* power pin, active low; -1 none */
static u32 sda, scl;
static int state = HRS_OFF;
static u8 id;
static u32 t_keep;

static void release(u32 p) { gpio_set_output_en(p, 0); }
static void low(u32 p) {
    gpio_write(p, 0);
    gpio_set_output_en(p, 1);
}
static void dly(void) { sleep_us(HALF_US); }

static void setup(u32 p) {
    gpio_set_func(p, AS_GPIO);
    gpio_write(p, 0);
    gpio_set_output_en(p, 0);
    gpio_set_input_en(p, 1);
    gpio_setup_up_down_resistor(p, PM_PIN_PULLUP_10K);
}

static void restore(u32 p) {
    gpio_set_output_en(p, 0);
    gpio_setup_up_down_resistor(p, PM_PIN_UP_DOWN_FLOAT);
}

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

static u8 get(void) {  /* one byte, not acknowledged (single reads only) */
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

static int rd(u8 reg, u8 *v) {
    int ok;
    start();
    ok = put(ADDR << 1) && put(reg);
    if (ok) {
        start();
        ok = put(ADDR << 1 | 1);
        if (ok) *v = get();
    }
    stop();
    return ok;
}

static int wr(u8 reg, u8 v) {
    int ok;
    start();
    ok = put(ADDR << 1) && put(reg) && put(v);
    stop();
    return ok;
}

static void power(int on) {
    u32 p;
    if (pwr_i < 0) return;
    p = allowed[pwr_i];
    if (on) {
        gpio_set_func(p, AS_GPIO);
        gpio_write(p, 0);
        gpio_set_input_en(p, 0);
        gpio_set_output_en(p, 1);
        sleep_us(20 * 1000);                       /* let the sensor's supply settle */
    } else {
        gpio_set_output_en(p, 0);                  /* released: floating, as at boot */
        gpio_set_input_en(p, 1);
    }
}

static void pins_off(void) {
    restore(sda);
    restore(scl);
    power(0);
}

int hrs_start(void) {
    u8 v;
    static const u8 init[][2] = {   /* the stock firmware's sequence */
        {0x16, 0x78}, {0x17, 0x0D}, {0x0C, 0x6E}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
        {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00}, {0x0B, 0x00},
        {0x01, 0xD0},               /* HEN on, a conversion every 50 ms (20 Hz) */
    };
    unsigned i;
    t_keep = clock_time();
    if (state == HRS_ON) return state;
    sda = allowed[sda_i];
    scl = allowed[scl_i];
    power(1);
    setup(sda);
    setup(scl);
    sleep_us(200);
    if (!gpio_read(sda) || !gpio_read(scl)) {      /* something holds a line low: hands off */
        pins_off();
        return state = HRS_ERR_LINES;
    }
    if (!rd(0x00, &v)) {
        pins_off();
        return state = HRS_ERR_NOACK;
    }
    id = v;
    if (v != 0x21) {
        pins_off();
        return state = HRS_ERR_ID;
    }
    for (i = 0; i < sizeof(init) / sizeof(init[0]); i++) wr(init[i][0], init[i][1]);
    return state = HRS_ON;
}

int hrs_state(void) { return state; }
u8 hrs_id(void) { return id; }
void hrs_keep(void) { t_keep = clock_time(); }

void hrs_poll(void) {
    if (state == HRS_OFF || clock_time() - t_keep < 1000u * 1000 * sys_tick_per_us) return;
    if (state == HRS_ON) wr(0x01, 0x00);           /* HEN off: LED and ADC off */
    pins_off();
    state = HRS_OFF;
}

int hrs_read(u32 *c0, u32 *c1) {
    u8 m0, h0, l0, m1, h1, l1;
    if (state != HRS_ON) return 0;
    if (!rd(0x09, &m0) || !rd(0x0A, &h0) || !rd(0x0F, &l0) ||
        !rd(0x08, &m1) || !rd(0x0D, &h1) || !rd(0x0E, &l1))
        return 0;
    *c0 = (u32)(l0 & 0x30) << 12 | (u32)m0 << 8 | (h0 & 0x0F) << 4 | (l0 & 0x0F);
    *c1 = (u32)(h1 & 0x3F) << 11 | (u32)m1 << 3 | (l1 & 0x07);
    return 1;
}

void hrs_power_text(char *out) {
    if (pwr_i < 0) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    out[0] = allowed_name[pwr_i][0];
    out[1] = allowed_name[pwr_i][1];
    out[2] = 0;
}

static int pin_index(const char *s);

int hrs_set_power(const char *s) {
    int p;
    if (state != HRS_OFF) return -1;
    if (s[0] == '-' && !s[1]) {
        pwr_i = -1;
        return 0;
    }
    if (!s[0] || !s[1] || s[2] || (p = pin_index(s)) < 0 || p == sda_i || p == scl_i) return -1;
    pwr_i = p;
    return 0;
}

/* ---- scan: levels at rest, then the usual sensor addresses on every pair of high pins ---- */
static const u8 scan_addr[] = {0x44, 0x33, 0x18, 0x19, 0x0F, 0x12, 0x13, 0x1E, 0x1F, 0x26, 0x27,
                               0x15, 0x68};
#define SCAN_HITS 6
static u16 levels;               /* bit i = allowed[i] high at rest; bit 15 = scan done */
static u8 hits[SCAN_HITS][3];    /* sda index, scl index, address */
static u8 n_hits;

static int probe(u8 addr) {
    int ack;
    start();
    ack = put(addr << 1);
    stop();
    return ack;
}

void hrs_scan(void) {
    int i, j, k;
    if (state != HRS_OFF) return;
    power(1);
    levels = 0;
    n_hits = 0;
    for (i = 0; i < N_ALLOWED; i++) {
        if (i == pwr_i) continue;
        setup(allowed[i]);
    }
    sleep_us(500);
    for (i = 0; i < N_ALLOWED; i++)
        if (i != pwr_i && gpio_read(allowed[i])) levels |= 1 << i;
    for (i = 0; i < N_ALLOWED; i++)
        for (j = 0; j < N_ALLOWED; j++) {
            if (i == j || !(levels >> i & 1) || !(levels >> j & 1)) continue;
            sda = allowed[i];
            scl = allowed[j];
            for (k = 0; k < (int)sizeof(scan_addr) && n_hits < SCAN_HITS; k++)
                if (probe(scan_addr[k])) {
                    hits[n_hits][0] = i;
                    hits[n_hits][1] = j;
                    hits[n_hits][2] = scan_addr[k];
                    n_hits++;
                }
            stop();
        }
    for (i = 0; i < N_ALLOWED; i++)
        if (i != pwr_i) restore(allowed[i]);
    power(0);
    levels |= 0x8000;
}

void hrs_scan_text(void (*put_s)(const char *s)) {
    int i;
    char t[12];
    if (!(levels & 0x8000)) {
        put_s("not run: write 1\n");
        return;
    }
    put_s("power ");
    hrs_power_text(t);
    put_s(t);
    put_s("\nidle");
    for (i = 0; i < N_ALLOWED; i++) {
        if (i == pwr_i) continue;
        t[0] = ' ';
        t[1] = allowed_name[i][0];
        t[2] = allowed_name[i][1];
        t[3] = '=';
        t[4] = levels >> i & 1 ? 'H' : 'L';
        t[5] = 0;
        put_s(t);
    }
    put_s("\n");
    for (i = 0; i < n_hits; i++) {         /* "A4 C7 44": SDA SCL address */
        t[0] = allowed_name[hits[i][0]][0];
        t[1] = allowed_name[hits[i][0]][1];
        t[2] = ' ';
        t[3] = allowed_name[hits[i][1]][0];
        t[4] = allowed_name[hits[i][1]][1];
        t[5] = ' ';
        t[6] = "0123456789abcdef"[hits[i][2] >> 4];
        t[7] = "0123456789abcdef"[hits[i][2] & 15];
        t[8] = '\n';
        t[9] = 0;
        put_s(t);
    }
    if (!n_hits) put_s("no device\n");
}

void hrs_pins_text(char *out) {
    const char *a = allowed_name[sda_i], *b = allowed_name[scl_i];
    out[0] = a[0]; out[1] = a[1]; out[2] = ' '; out[3] = b[0]; out[4] = b[1]; out[5] = 0;
}

static int pin_index(const char *s) {
    int i;
    char c0 = s[0] >= 'a' ? s[0] - 32 : s[0];
    for (i = 0; i < N_ALLOWED; i++)
        if (allowed_name[i][0] == c0 && allowed_name[i][1] == s[1]) return i;
    return -1;
}

int hrs_set_pins(const char *s) {  /* "A4 C7": SDA SCL */
    int a, b;
    while (*s == ' ') s++;
    if (!s[0] || !s[1] || (a = pin_index(s)) < 0) return -1;
    s += 2;
    while (*s == ' ') s++;
    if (!s[0] || !s[1] || (b = pin_index(s)) < 0 || s[2] || a == b || a == pwr_i || b == pwr_i)
        return -1;
    if (state != HRS_OFF) return -1;               /* not while it is in use */
    sda_i = a;
    scl_i = b;
    return 0;
}
