/*
 * LT716 battery monitor (micro firmware, no Bluetooth).
 *
 * Shows on the screen, once a second: battery voltage (PB1, 1:4 divider), charger rail (PB2,
 * 1:8 divider, 4400-6500 mV = charging), how much the battery moved since start, and the time
 * left. The radio stays off, so a weak battery is not pulled down by it.
 *
 * Going back: after RUN_S seconds off the charger (on the charger it stays), or with the touch
 * key held HOLD_MS, it clears its own magic
 * at 0x8 and reboots: the ROM then starts the vendor updater at 0x20000, ready for an OTA.
 * Safeguard as in MorseOS 0.5.7: hardware watchdog fed only by the main loop, and a boot counter
 * (analog register 0x3c) that sends the band to the updater after 3 boots in a row without
 * HEALTHY_MS of healthy loop.
 */
#include "drivers/5316/bsp.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/compiler.h"
#include "drivers/5316/driver_5316.h"
#include "drivers/5316/flash.h"
#include "drivers/5316/gpio.h"
#include "drivers/5316/register.h"
#include "drivers/5316/pm.h"
#include "drivers/5316/timer.h"
#include "st7735.h"
#include "font.h"
#include "battery.h"

#define PIN_MOTOR  GPIO_PA5   /* active high */
#define PIN_TOUCH  GPIO_PC2   /* active high while touched */
#define RUN_S      600        /* then back to the updater */
#define HOLD_MS    3000
#define SCREEN_S   30         /* screen off after this many seconds without a touch */

#define SG_REG     DEEP_ANA_REG2
#define SG_MAGIC   0xA0
#define SG_LIMIT   3
#define HEALTHY_MS 10000
#define WD_MS      4000
#define TICKS_PER_MS 16000

extern void start_reboot(void);   /* liblt_5316.a (stack/ble/service/ble_ll_ota.h) */

_attribute_ram_code_ void irq_handler(void) {}

static void wd_arm(void) {
    u32 capt = ((u32)WD_MS * TICKS_PER_MS) >> WATCHDOG_TIMEOUT_COEFF;
    reg_tmr_sta = FLD_TMR_STA_WD;
    reg_tmr_ctrl = (reg_tmr_ctrl & 0x1ff) | ((capt & 0x3fff) << 9) | FLD_TMR_WD_EN;
}

static void wd_feed(void) {
    if (!(reg_tmr_ctrl & FLD_TMR_WD_EN)) wd_arm();
    reg_tmr_sta = FLD_TMR_STA_WD;
}

/* same checks as MorseOS: only from bank 0, only if the updater is there */
static void enter_updater(void) {
    u8 h1[12], zero[4] = {0, 0, 0, 0};
    if (REG_ADDR8(0x63e) != 0) return;
    flash_read_page(0x20000, sizeof(h1), h1);
    if (h1[8] != 0x4b || h1[9] != 'N' || h1[10] != 'L' || h1[11] != 'T') return;
    flash_write_page(0x8, 4, zero);
    sleep_us(100 * 1000);
    start_reboot();
}

/* decimal without libc; neg adds a leading '-' */
static char *num(char *p, u32 v) {
    char t[10];
    int n = 0;
    do {
        t[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void line(int y, const char *label, u32 v, const char *unit, u16 color) {
    char s[24], *p = s;
    while (*label) *p++ = *label++;
    p = num(p, v);
    while (*unit) *p++ = *unit++;
    while (p < s + 12) *p++ = ' ';  /* clear the rest of the old text */
    *p = 0;
    font_text(2, y, s, 1, color, C_BLACK);
}

int main(void) {
    cpu_wakeup_init();
    clock_init(SYS_CLK_16M_Crystal);
    wd_arm();
    gpio_init();

    {
        u8 r = analog_read(SG_REG);
        u8 n = (r & 0xf0) == SG_MAGIC ? (r & 0x0f) : 0;
        if (n < 15) n++;
        analog_write(SG_REG, SG_MAGIC | n);
        if (n >= SG_LIMIT) {
            analog_write(SG_REG, SG_MAGIC);
            enter_updater();
        }
    }

    gpio_set_func(PIN_MOTOR, AS_GPIO);
    gpio_set_output_en(PIN_MOTOR, 1);
    gpio_set_input_en(PIN_MOTOR, 0);
    gpio_write(PIN_MOTOR, 0);
    gpio_set_func(PIN_TOUCH, AS_GPIO);
    gpio_set_output_en(PIN_TOUCH, 0);
    gpio_set_input_en(PIN_TOUCH, 1);

    lcd_init();
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    lcd_backlight(1);
    font_text(2, 4, "BATTERIA", 1, C_YELLOW, C_BLACK);

    battery_update();
    u32 start_mv = battery_mv(), secs = 0, left = RUN_S, idle = 0, t_sec = clock_time(), t_key = 0;
    int healthy = 0, awake = 1;
    while (1) {
        wd_feed();
        if (!clock_time_exceed(t_sec, 1000 * 1000)) {
            int t = gpio_read(PIN_TOUCH) ? 1 : 0;
            if (t) {
                if (!t_key) t_key = clock_time();
                else if (clock_time_exceed(t_key, HOLD_MS * 1000)) break;
                idle = 0;
                if (!awake) {           /* a touch wakes the screen */
                    awake = 1;
                    lcd_sleep(0);
                    lcd_backlight(1);
                }
            } else {
                t_key = 0;
            }
            continue;
        }
        t_sec = clock_time();
        secs++;
        if (!healthy && secs * 1000 >= HEALTHY_MS) {
            healthy = 1;
            analog_write(SG_REG, SG_MAGIC);
        }

        battery_update();
        u32 mv = battery_mv(), ch = battery_charger_mv();
        int charging = battery_charging();
        /* on the charger battmon stays: the countdown restarts when the charger is removed */
        if (charging) left = RUN_S;
        else if (left) left--;
        if (!left) break;
        if (awake && ++idle >= SCREEN_S) {
            awake = 0;
            lcd_backlight(0);
            lcd_sleep(1);
        }
        if (!awake) continue;

        line(24, "BAT ", mv, " MV", C_WHITE);
        line(36, "CAR ", ch, " MV", charging ? C_GREEN : C_GREY);
        font_text(2, 52, charging ? "IN CARICA   " : "NON CARICA  ", 1, charging ? C_GREEN : C_RED,
                  C_BLACK);
        {
            /* change since start: SALE (up) / SCENDE (down) */
            char s[24], *p = s;
            const char *w = mv >= start_mv ? "SALE " : "SCENDE ";
            while (*w) *p++ = *w++;
            p = num(p, mv >= start_mv ? mv - start_mv : start_mv - mv);
            *p++ = ' ';
            *p++ = 'M';
            *p++ = 'V';
            while (p < s + 12) *p++ = ' ';
            *p = 0;
            font_text(2, 68, s, 1, mv >= start_mv ? C_GREEN : C_RED, C_BLACK);
        }
        line(84, "T ", secs, " S", C_WHITE);
        if (charging) font_text(2, 130, "RESTA ACCESO", 1, C_GREEN, C_BLACK);
        else line(130, "FINE ", left, " S", C_GREY);
        font_text(2, 142, "TIENI 3S ESCI", 1, C_GREY, C_BLACK);
    }

    /* say where it is going: the updater keeps the screen off and ignores the key, so the band
     * would otherwise look dead (that is how the first band was taken for broken) */
    lcd_sleep(0);
    lcd_backlight(1);
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    font_text(2, 40, "UPDATER", 1, C_YELLOW, C_BLACK);
    font_text(2, 60, "SCHERMO", 1, C_WHITE, C_BLACK);
    font_text(2, 72, "SPENTO", 1, C_WHITE, C_BLACK);
    font_text(2, 92, "NON E MORTA", 1, C_GREEN, C_BLACK);
    font_text(2, 104, "SOLO BLE", 1, C_GREY, C_BLACK);
    {
        int i, k;
        for (i = 0; i < 2; i++) {               /* two buzzes */
            gpio_write(PIN_MOTOR, 1);
            for (k = 0; k < 2; k++) { wd_feed(); sleep_us(100 * 1000); }
            gpio_write(PIN_MOTOR, 0);
            for (k = 0; k < 2; k++) { wd_feed(); sleep_us(100 * 1000); }
        }
        for (k = 0; k < 30; k++) { wd_feed(); sleep_us(100 * 1000); }   /* 3 s to read it */
    }
    enter_updater();
    while (1) wd_feed();  /* refused (no updater): stay here, screen on */
}
