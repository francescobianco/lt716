/* MorseOS HRS3300 driver, see hrs3300.h. Bit-banged I2C as lib/i2cprobe (~50 kHz). */
#include "hrs3300.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"
#include "st7735.h"

#define ADDR    0x44
#define HALF_US 10

/* pins that may be tried: not the display, motor, touch key, battery ADC or SPI ones. The
 * last three (0.8.10) are the first band's display DC (PC1) and backlight (PB3) and PB0:
 * unused on the second band, so tried only there (lcd_board, found at boot) */
static const u32 allowed[] = {GPIO_PA3, GPIO_PA4, GPIO_PA7, GPIO_PB4, GPIO_PB5, GPIO_PB6,
                              GPIO_PB7, GPIO_PC0, GPIO_PC4, GPIO_PC7,
                              GPIO_PB0, GPIO_PB3, GPIO_PC1};
static const char *const allowed_name[] = {"A3", "A4", "A7", "B4", "B5", "B6", "B7", "C0",
                                           "C4", "C7", "B0", "B3", "C1"};
#define N_ALLOWED 13
#define N_COMMON  10

static int pin_ok(int i) { return i < N_COMMON || (lcd_board && lcd_board->band == 2); }

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

/* A slave stopped in the middle of a byte (e.g. by a reset) keeps SDA low and ignores
 * everything: clock it out with up to 9 pulses on its SCL, then a stop. The stock firmware
 * shares SDA = PA4 between the HRS3300 (SCL = PC7, bit-banged) and, on the hardware I2C,
 * most likely the accelerometer (SCL = PA3): either can hold it. 1 = SDA is high now. */
static int unstick(u32 d, u32 c) {
    int i;
    setup(c);
    sleep_us(50);
    if (!gpio_read(c)) {                           /* this clock line is held too: not ours */
        restore(c);
        return gpio_read(d) ? 1 : 0;
    }
    release(d);
    for (i = 0; i < 9 && !gpio_read(d); i++) {
        low(c); sleep_us(HALF_US);
        release(c); sleep_us(HALF_US);
    }
    low(d); sleep_us(HALF_US);                     /* stop: SDA rises while SCL is high */
    release(c); sleep_us(HALF_US);
    release(d); sleep_us(HALF_US);
    return gpio_read(d) ? 1 : 0;
}

#define I_A3 0
#define I_C7 9
static s8 unstuck_by = -2;       /* last recovery: -2 none needed, -1 failed, else pin index */

/* SDA (index si) low at rest: try PC7, then PA3, as its clock (unless used otherwise) */
static int recover_sda(int si) {
    static const u8 clocks[2] = {I_C7, I_A3};
    int k;
    for (k = 0; k < 2; k++) {
        int c = clocks[k];
        if (c == si || c == pwr_i) continue;
        if (unstick(allowed[si], allowed[c])) {
            if (c != scl_i) restore(allowed[c]);
            unstuck_by = c;
            return 1;
        }
        if (c != scl_i) restore(allowed[c]);
    }
    unstuck_by = -1;
    return 0;
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
    unstuck_by = -2;
    if (gpio_read(scl) && !gpio_read(sda)) recover_sda(sda_i);   /* 0.8.9: a stuck bus */
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
        if (i == pwr_i || !pin_ok(i)) continue;
        setup(allowed[i]);
    }
    sleep_us(500);
    unstuck_by = -2;
    if (!gpio_read(allowed[sda_i])) recover_sda(sda_i);   /* the sensor's SDA held low */
    for (i = 0; i < N_ALLOWED; i++)
        if (i != pwr_i && pin_ok(i)) setup(allowed[i]);
    sleep_us(200);
    for (i = 0; i < N_ALLOWED; i++)
        if (i != pwr_i && pin_ok(i) && gpio_read(allowed[i])) levels |= 1 << i;
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
        if (i != pwr_i && pin_ok(i)) restore(allowed[i]);
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
        if (i == pwr_i || !pin_ok(i)) continue;
        t[0] = ' ';
        t[1] = allowed_name[i][0];
        t[2] = allowed_name[i][1];
        t[3] = '=';
        t[4] = levels >> i & 1 ? 'H' : 'L';
        t[5] = 0;
        put_s(t);
    }
    put_s("\nunstick ");
    if (unstuck_by == -2) put_s("not needed");
    else if (unstuck_by == -1) put_s("failed");
    else {
        t[0] = allowed_name[unstuck_by][0];
        t[1] = allowed_name[unstuck_by][1];
        t[2] = 0;
        put_s("ok with ");
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

/* ---- diagnostics: drive pins by hand, find the LED and the photodiode ---- */
static u8 drv_mode[N_ALLOWED];   /* 0 released, 1 low, 2 high */

int hrs_pin_cmd(const char *s) {
    int i;
    u32 p;
    while (*s == ' ') s++;
    if (!s[0] || !s[1] || (i = pin_index(s)) < 0 || i == sda_i || i == scl_i) return -1;
    if (state != HRS_OFF) return -1;
    s += 2;
    while (*s == ' ') s++;
    p = allowed[i];
    if ((s[0] == '0' || s[0] == '1') && !s[1]) {
        gpio_set_func(p, AS_GPIO);
        gpio_setup_up_down_resistor(p, PM_PIN_UP_DOWN_FLOAT);
        gpio_write(p, s[0] == '1');
        gpio_set_input_en(p, 0);
        gpio_set_output_en(p, 1);
        drv_mode[i] = s[0] == '1' ? 2 : 1;
        return 0;
    }
    if ((s[0] == 'z' || s[0] == 'Z') && !s[1]) {
        gpio_set_output_en(p, 0);
        gpio_set_input_en(p, 1);
        drv_mode[i] = 0;
        return 0;
    }
    return -1;
}

void hrs_pin_text(void (*put_s)(const char *s)) {
    int i, any = 0;
    char t[6];
    for (i = 0; i < N_ALLOWED; i++)
        if (drv_mode[i]) {
            t[0] = allowed_name[i][0];
            t[1] = allowed_name[i][1];
            t[2] = ' ';
            t[3] = drv_mode[i] == 2 ? '1' : '0';
            t[4] = '\n';
            t[5] = 0;
            put_s(t);
            any = 1;
        }
    if (!any) put_s("none\n");
}

u32 hrs_adc_pin(const char *n) {
    static const u32 adc_pins[] = {GPIO_PA7, GPIO_PB0, GPIO_PB1, GPIO_PB2, GPIO_PB3, GPIO_PB4,
                                   GPIO_PB5, GPIO_PB6, GPIO_PB7};
    static const char *const adc_names[] = {"A7", "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7"};
    int i;
    char c0 = n[0] >= 'a' ? n[0] - 32 : n[0];
    for (i = 0; i < 9; i++)
        if (adc_names[i][0] == c0 && adc_names[i][1] == n[1] && !n[2]) {
            if (adc_pins[i] == GPIO_PB3 && !(lcd_board && lcd_board->band == 2)) return 0;
            return adc_pins[i];
        }
    return 0;
}

static s8 adc_pull = -1;       /* -1 leave, else PM_PIN_* */
static const char *const pull_names[] = {"f", "u1m", "u10k", "d100k"};

int hrs_adc_pull_set(const char *s) {
    int i;
    if (s[0] == '-' && !s[1]) {
        adc_pull = -1;
        return 0;
    }
    for (i = 0; i < 4; i++) {
        const char *a = pull_names[i], *b = s;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) {
            adc_pull = i;       /* PM_PIN_UP_DOWN_FLOAT 0, PULLUP_1M 1, PULLUP_10K 2, PULLDOWN_100K 3 */
            return 0;
        }
    }
    return -1;
}

const char *hrs_adc_pull_name(void) { return adc_pull < 0 ? "-" : pull_names[(int)adc_pull]; }

void hrs_adc_pull_apply(u32 pin) {
    if (adc_pull < 0) return;
    gpio_setup_up_down_resistor(pin, adc_pull);
    sleep_us(2000);             /* let the node settle (a photodiode node is slow) */
}

void hrs_pins_text(char *out) {
    const char *a = allowed_name[sda_i], *b = allowed_name[scl_i];
    out[0] = a[0]; out[1] = a[1]; out[2] = ' '; out[3] = b[0]; out[4] = b[1]; out[5] = 0;
}

static int pin_index(const char *s) {
    int i;
    char c0 = s[0] >= 'a' ? s[0] - 32 : s[0];
    for (i = 0; i < N_ALLOWED; i++)
        if (pin_ok(i) && allowed_name[i][0] == c0 && allowed_name[i][1] == s[1]) return i;
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
