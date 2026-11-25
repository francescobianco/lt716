/*
 * SVEGLIA: an alarm is ringing. The screen lights up with the time; a vibration every 1.5 s
 * (also with IMPOSTAZIONI > VIBRAZIONE off), a yellow frame that blinks with it. Any touch
 * stops it; after 60 s it stops by itself. Back to the watch face.
 */
#include "mos.h"
#include "alarm.h"
#include "font.h"
#include "rtc.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define RING_S    60
#define PULSE_MS  1500
#define BUZZ_MS   400

static u32 t_start, t_pulse;
static int frame_on;

static void frame(int on) {
    u16 c = on ? C_YELLOW : C_BLACK;
    lcd_fill(0, 0, lcd.width, 3, c);
    lcd_fill(0, lcd.height - 3, lcd.width, 3, c);
    lcd_fill(0, 3, 3, lcd.height - 6, c);
    lcd_fill(lcd.width - 3, 3, 3, lcd.height - 6, c);
    frame_on = on;
}

static void draw(void) {
    rtc_time_t t;
    char s[6], *p = s;
    rtc_get(&t);
    p = mos_num(p, t.hour, 2);
    *p++ = ':';
    mos_num(p, t.min, 2);
    lcd_fill(3, 3, lcd.width - 6, lcd.height - 6, C_BLACK);
    frame(1);
    mos_center_text(30, "SVEGLIA", 1, C_YELLOW);
    mos_center_text(64, s, 2, C_WHITE);
    mos_center_text(130, "TOCCA PER", 1, C_GREY);
    mos_center_text(140, "FERMARE", 1, C_GREY);
}

static void enter(void) { t_start = t_pulse = clock_time(); }

static void tick(void) {
    if (mos_ms_since(t_start) > RING_S * 1000) {
        mos_home();
        return;
    }
    if (mos_ms_since(t_pulse) >= PULSE_MS) {
        t_pulse = clock_time();
        frame(1);
        mos_buzz_always(BUZZ_MS);   /* blocks BUZZ_MS: the watchdog allows 4 s */
        frame(0);
    }
}

static void key(int ev) { mos_home(); }

const mos_app_t app_alarm = {"alarm", 0, RING_S + 10, enter, draw, tick, key, 0, 0, 0, 0, 0};

void alarm_ring(void) {
    mos_open(&app_alarm);
    mos_wake();
    mos_buzz_always(BUZZ_MS);
}
