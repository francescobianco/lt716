/* Bit-banged I2C at ~50 kHz. A line is driven low or released (input + pull-up): never
 * driven high, so nothing can fight it. Lines found low at rest are left alone. */
#include "i2cprobe.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

/* PC7 is the SWS pad: the stock firmware runs its software I2C on SDA = PA4, SCL = PC7 */
static const u32 pins[] = {GPIO_PA3, GPIO_PA4, GPIO_PC4, GPIO_PC7};
static const char *const names[] = {"A3", "A4", "C4", "C7"};
#define NPINS 4
#define HALF_US 10

static u32 sda, scl;

const char *i2cprobe_pin_name(int i) { return i >= 0 && i < NPINS ? names[i] : "?"; }

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

/* returns 1 if the byte was acknowledged */
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

static u8 get(int ack) {
    int i;
    u8 b = 0;
    release(sda);
    for (i = 0; i < 8; i++) {
        dly();
        release(scl); dly();
        b = b << 1 | (gpio_read(sda) ? 1 : 0);
        low(scl);
    }
    if (ack) low(sda); else release(sda);
    dly();
    release(scl); dly();
    low(scl); dly();
    release(sda);
    return b;
}

/* a slave stuck in the middle of a byte holds SDA low: clock it out (up to 9 pulses) */
static int recover(void) {
    int i;
    release(sda);
    for (i = 0; i < 9 && !gpio_read(sda); i++) {
        low(scl); dly();
        release(scl); dly();
    }
    stop();
    return gpio_read(sda) ? 1 : 0;
}

static int probe(u8 addr) {
    int ack;
    start();
    ack = put(addr << 1);
    stop();
    return ack;
}

static int read_reg(u8 addr, u8 reg, u8 *val) {
    int ok;
    start();
    ok = put(addr << 1) && put(reg);
    if (ok) {
        start();                       /* repeated start */
        ok = put(addr << 1 | 1);
        if (ok) *val = get(0);
    }
    stop();
    return ok;
}

/* the ID register of the sensors these bands use */
static u8 id_reg_for(u8 addr) {
    switch (addr) {
    case 0x44: return 0x00;   /* HRS3300 heart rate: 0x21 */
    case 0x18: case 0x19: return 0x0F;   /* SC7A20 / LIS3DH-like accelerometers */
    case 0x0F: case 0x1F: return 0x00;   /* STK8321 / STK8323 / KXTJ3 (0x0F) */
    case 0x26: case 0x27: return 0x01;   /* DA213 / DA217 */
    case 0x12: case 0x13: return 0x00;   /* QMA7981 */
    default: return 0x00;
    }
}

const char *i2cprobe_guess(const i2cprobe_hit_t *h) {
    if (h->id_reg == 0xFF) return "?";
    if (h->addr == 0x44 && h->id == 0x21) return "HRS3300";
    if ((h->addr == 0x18 || h->addr == 0x19) && h->id == 0x11) return "SC7A20";
    if ((h->addr == 0x18 || h->addr == 0x19) && h->id == 0x33) return "LIS3DH";
    if ((h->addr == 0x0F || h->addr == 0x1F) && (h->id == 0x23 || h->id == 0x21)) return "STK832X";
    if (h->addr == 0x0F && h->id == 0x35) return "KXTJ3";
    if ((h->addr == 0x26 || h->addr == 0x27) && h->id == 0x13) return "DA213";
    if ((h->addr == 0x12 || h->addr == 0x13) && h->id == 0xE7) return "QMA7981";
    return "?";
}

static void scan_pair(i2cprobe_result_t *r, int a, int b) {
    u8 addr;
    sda = pins[a];
    scl = pins[b];
    if (!gpio_read(scl)) return;             /* clock line held low: not a bus */
    if (!gpio_read(sda) && !recover()) return;
    for (addr = 0x08; addr < 0x78; addr++) {
        i2cprobe_hit_t *h;
        if (r->n == I2CPROBE_MAX || !probe(addr)) continue;
        h = &r->hit[r->n++];
        h->sda = a;
        h->scl = b;
        h->addr = addr;
        h->id_reg = id_reg_for(addr);
        if (!read_reg(addr, h->id_reg, &h->id)) h->id_reg = 0xFF;
    }
}

void i2cprobe_run(i2cprobe_result_t *r) {
    int a, b, i;
    r->n = 0;
    r->busy_mask = 0;
    for (i = 0; i < NPINS; i++) setup(pins[i]);
    sleep_us(200);
    for (i = 0; i < NPINS; i++)
        if (!gpio_read(pins[i])) r->busy_mask |= 1 << i;   /* held low by something */
    scan_pair(r, 1, 3);                      /* the stock wiring first: SDA PA4, SCL PC7 */
    for (a = 0; a < NPINS; a++)
        for (b = 0; b < NPINS; b++)
            if (a != b && !(a == 1 && b == 3) && !(r->busy_mask >> b & 1)) scan_pair(r, a, b);
    for (i = 0; i < NPINS; i++) restore(pins[i]);
}
