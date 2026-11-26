/* MorseOS motion sensor and step counter (vibration switch on PA4), see motion.h. */
#include "motion.h"
#include "bundle.h"   /* MOS_HAVE_RTC */
#include "rtc.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

#define PIN GPIO_PA4

static int level = -1;
static u32 edges, hits, steps, pending;
static u32 t_edge, t_hit;          /* clock_time of the last edge / hit */
static int have_edge, have_hit, run, last_day = -1;
static u16 quiet_ms = 150, min_ms = 250, max_ms = 2000;
static u8 confirm = 4;

static u32 ms_since(u32 t) { return (clock_time() - t) / (sys_tick_per_us * 1000); }

void motion_init(void) {     /* as the stock firmware: GPIO input, 10k pull-up */
    gpio_set_func(PIN, AS_GPIO);
    gpio_set_output_en(PIN, 0);
    gpio_set_input_en(PIN, 1);
    gpio_setup_up_down_resistor(PIN, PM_PIN_PULLUP_10K);
    level = gpio_read(PIN) ? 1 : 0;
}

static void new_hit(void) {
    u32 gap = have_hit ? ms_since(t_hit) : 0;
    hits++;
    t_hit = clock_time();
    if (!have_hit || gap > max_ms) {          /* first, or after a pause: start over */
        have_hit = 1;
        run = 1;
        pending = 1;
        return;
    }
    if (gap < min_ms) return;                 /* too fast for a step: shaking, not walking */
    run++;
    if (run < confirm) {
        pending++;
    } else if (run == confirm) {
        steps += pending + 1;                 /* the run is a walk: count it all */
        pending = 0;
    } else {
        steps++;
    }
}

void motion_poll(void) {
    int v;
    if (level < 0) return;
#ifdef MOS_HAVE_RTC
    if (rtc_is_set()) {                       /* a new day: start from zero */
        rtc_time_t t;
        rtc_get(&t);
        if (last_day >= 0 && t.day != last_day) steps = pending = 0, run = 0;
        last_day = t.day;
    }
#endif
    v = gpio_read(PIN) ? 1 : 0;
    if (v == level) return;
    level = v;
    edges++;
    if (!have_edge || ms_since(t_edge) >= quiet_ms) new_hit();
    have_edge = 1;
    t_edge = clock_time();
}

int motion_level(void) { return level; }
int motion_wake_level(void) { return level ? 0 : 1; }
u32 motion_edges(void) { return edges; }
void motion_clear(void) { edges = hits = 0; }
u32 motion_hits(void) { return hits; }
u32 steps_today(void) { return steps; }
void steps_reset(void) { steps = pending = 0; run = 0; }
u32 steps_distance_m(void) { return steps * 70 / 100; }
u32 steps_kcal(void) { return steps * 4 / 100; }

static char *num(char *p, u32 v) {
    char t[10];
    int n = 0;
    do t[n++] = '0' + v % 10, v /= 10; while (v);
    while (n) *p++ = t[--n];
    return p;
}

void steps_cfg_text(char *out) {
    out = num(out, quiet_ms); *out++ = ' ';
    out = num(out, min_ms); *out++ = ' ';
    out = num(out, max_ms); *out++ = ' ';
    out = num(out, confirm);
    *out = 0;
}

static const char *rd(const char *s, u32 *v) {
    u32 n = 0;
    int any = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0'), any = 1;
    *v = n;
    return any ? s : 0;
}

int steps_set_cfg(const char *s) {
    u32 q, a, b, c;
    if (!(s = rd(s, &q)) || !(s = rd(s, &a)) || !(s = rd(s, &b)) || !(s = rd(s, &c)) || *s)
        return -1;
    if (q < 10 || q > 1000 || a < 100 || b > 5000 || a >= b || c < 1 || c > 10) return -1;
    quiet_ms = q;
    min_ms = a;
    max_ms = b;
    confirm = c;
    return 0;
}
