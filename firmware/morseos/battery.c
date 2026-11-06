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

void battery_update(void) {
    if (!inited) {
        adc_init();
        inited = 1;
    }
    vbat_mv = sample(PIN_VBAT) * 4;
    charger_mv = sample(PIN_CHARGER) * 8;
}

u32 battery_mv(void) { return vbat_mv; }

int battery_level(void) {
    if (vbat_mv >= 3800) return 100;
    if (vbat_mv >= 3700) return 75;
    if (vbat_mv >= 3600) return 50;
    if (vbat_mv >= 3500) return 25;
    return 0;
}

int battery_charging(void) { return charger_mv >= 4400 && charger_mv <= 6500; }
