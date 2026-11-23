/*
 * Battery: PB1 through the PCB's 1:4 divider; charger rail: PB2 through 1:8.
 * Board facts (dividers, ~1.152 mV per count at the pin with the SDK's default ADC
 * setup, the stock gauge steps and the 4400 mV "external supply" threshold) come from
 * the FitPro-LT715-TLSR8232 documentation of the stock firmware.
 */
#include "battery.h"
#include "drivers/5316/adc.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

#define PIN_VBAT    GPIO_PB1
#define PIN_CHARGER GPIO_PB2

static volatile signed short buf[32];
static u32 vbat_mv = 4000, charger_mv;
static int inited;

/* 16 conversions, sorted, middle 8 averaged: the motor and backlight dent the rail */
static u32 sample(GPIO_PinTypeDef pin) {
    unsigned short v[16], x;
    int i, j;
    u32 t0, sum = 0;

    adc_base_init(pin);
    adc_reset();
    aif_reset();
    adc_power_on(1);
    for (i = 0; i < 32; i++) buf[i] = 0;
    sleep_us(25);
    adc_aif_set_misc_buf((unsigned short *)buf, 32);
    adc_aif_set_m_chn_en(1);
    adc_aif_set_use_raw_data_en();
    for (i = 0; i < 16; i++) {
        t0 = clock_time();
        while (!buf[i] && !clock_time_exceed(t0, 50));
        v[i] = (buf[i] & (1 << 13)) ? 0 : (buf[i] & 0x1FFF);  /* bit 13 = out of range */
    }
    adc_power_on(0);
    adc_aif_set_m_chn_en(0);
    for (i = 1; i < 16; i++) {
        x = v[i];
        for (j = i - 1; j >= 0 && v[j] > x; j--) v[j + 1] = v[j];
        v[j + 1] = x;
    }
    for (i = 4; i < 12; i++) sum += v[i];
    return ((sum / 8) * 295) >> 8;  /* mV at the pin */
}

static int full;            /* charge complete (see battery_full) */
static u32 t_ref, ref_mv;
static int ref_set;

void battery_update(void) {
    if (!inited) {
        adc_init();
        inited = 1;
    }
    vbat_mv = sample(PIN_VBAT) * 4;
    charger_mv = sample(PIN_CHARGER) * 8;

    /* charge complete: the charger IC (CC/CV to 4.20 V) decides, the firmware only watches.
     * Full = above 4150 mV and risen less than 10 mV in 4 minutes (the CV phase has ended). */
    if (!battery_charging()) {
        full = 0;
        ref_set = 0;
    } else if (!ref_set) {
        ref_set = 1;
        t_ref = clock_time();
        ref_mv = vbat_mv;
    } else if (clock_time_exceed(t_ref, 240u * 1000 * 1000)) {   /* < 268 s tick wrap */
        if (vbat_mv >= 4150 && vbat_mv <= ref_mv + 10) full = 1;
        t_ref = clock_time();
        ref_mv = vbat_mv;
    }
}

void battery_poll(void) {
    static u32 t_last;
    static int started;
    if (!started || clock_time_exceed(t_last, 30 * 1000 * 1000)) {
        started = 1;
        t_last = clock_time();
        battery_update();
    }
}

u32 battery_mv(void) { return vbat_mv; }

int battery_charging(void) { return charger_mv >= 4400 && charger_mv <= 6500; }

int battery_full(void) { return full; }

/* percent from a typical Li-ion rest curve; on the charger the cell reads about 120 mV high */
int battery_level(void) {
    static const u16 mv[] = {3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    static const u8 pct[] = {5, 15, 30, 50, 65, 80, 90, 100};
    u32 v = vbat_mv;
    int i;
    if (battery_charging()) {
        if (full) return 100;
        v = v > 120 ? v - 120 : 0;
    }
    if (v < mv[0]) return v > 3300 ? (int)((v - 3300) * 5 / 200) : 0;   /* 3300..3500: 0..5 */
    for (i = 1; i < 8; i++)
        if (v < mv[i])
            return pct[i - 1] + (int)((v - mv[i - 1]) * (pct[i] - pct[i - 1]) / (mv[i] - mv[i - 1]));
    return battery_charging() ? 99 : 100;   /* on the charger 100 only once full */
}
