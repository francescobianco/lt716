/*
 * SALUTE > BATTITO: a plain heart-rate reader that shows how it works, step by step.
 *
 *   LED  C0, light of the green LED reflected by the wrist (+ ambient), 20 samples a second
 *   AMB  C1, ambient light alone
 *   DC   slow average of C0 (~1.6 s): the steady part (skin, tissue, how tight the band is)
 *   AC   DC - C0, lightly smoothed: the pulse. Each beat brings more blood, which absorbs
 *        more green light, so C0 dips: inverted, a beat is a rise
 *   PEN  slope of AC over 100 ms: the upstroke of a beat is the fastest change in the signal,
 *        much faster than the drift of the light when the band moves (which skews AC), so
 *        the beats are found on the slope
 *   SOG  threshold: half of the recent slope peaks (decays in ~3 s)
 *   IBI  time between two beats (slope above the threshold, at least 300 ms apart)
 *   BPM  60000 / the average of the last 4 IBIs between 300 and 1500 ms
 * Below, the AC wave (centred) sweeps like a monitor, a beat as a red tick.
 *   tap   retry the sensor (after an error) / clear the beats
 *   hold 3 s back (the sensor switches off)
 */
#include "mos.h"
#include "aafont.h"
#include "hrs3300.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

#define SAMPLE_MS 50
#define WAVE_Y    100
#define WAVE_H    40
#define IBI_N     4

static u32 t_sample, t_text, t_now_ms, t_peak_ms;
static s32 dc16, s4, env, mid8, s_hist[2], slope;
static u32 c0, c1;
static int above, x, prev_y, n_ibi, bpm, errs, beats, st;  /* st: hrs_start() result */
static u16 ibi[IBI_N];

static void reset_signal(void) {
    dc16 = 0;
    s4 = env = mid8 = slope = 0;
    s_hist[0] = s_hist[1] = 0;
    above = n_ibi = bpm = beats = 0;
    t_peak_ms = 0;
    x = 0;
    prev_y = WAVE_Y + WAVE_H / 2;
}

static char *num_s(char *p, s32 v) {
    if (v < 0) *p++ = '-', v = -v;
    return mos_num(p, v, 1);
}

static void line(int y, const char *label, s32 v, const char *unit) {
    char t[24], *p = t;
    while (*label) *p++ = *label++;
    *p++ = ' ';
    p = num_s(p, v);
    if (unit) while (*unit) *p++ = *unit++;
    *p = 0;
    aafont_line(&aafont_small, y, t, C_WHITE, C_BLACK);
}

static void draw_error(void) {
    char t[16], *p = t;
    const char *why = st == HRS_ERR_LINES ? "LINEE BASSE" : st == HRS_ERR_NOACK ? "NON RISPONDE" : 0;
    aafont_line(&aafont_medium, 16, "SENSORE?", C_RED, C_BLACK);
    if (!why) {                                 /* "ID 3F NON 21" */
        *p++ = 'I'; *p++ = 'D'; *p++ = ' ';
        *p++ = "0123456789ABCDEF"[hrs_id() >> 4];
        *p++ = "0123456789ABCDEF"[hrs_id() & 15];
        *p = 0;
        why = t;
    }
    aafont_line(&aafont_small, 40, why, C_WHITE, C_BLACK);
    p = t;
    *p++ = 'P'; *p++ = 'I'; *p++ = 'N'; *p++ = ' ';
    hrs_pins_text(p);
    aafont_line(&aafont_small, 52, t, C_GREY, C_BLACK);
    aafont_line(&aafont_small, 64, "TAP RIPROVA", C_GREY, C_BLACK);
}

static void draw_text(void) {
    char t[12], *p = t;
    s32 dc = dc16 / 16;
    if (bpm) {
        p = mos_num(p, bpm, 1);
    } else {
        *p++ = '-';
        *p++ = '-';
    }
    *p++ = ' '; *p++ = 'B'; *p++ = 'P'; *p++ = 'M';
    *p = 0;
    aafont_line(&aafont_medium, 14, t, bpm ? C_RED : C_GREY, C_BLACK);
    line(33, "LED", c0, 0);
    line(42, "AMB", c1, 0);
    line(51, "DC", dc, 0);
    line(60, "AC", s4 / 4, 0);
    line(69, "PEN", slope, 0);
    line(78, "SOG", env / 2, 0);
    line(87, "IBI", n_ibi ? ibi[(n_ibi - 1) % IBI_N] : 0, " MS");
}

static void enter(void) {
    reset_signal();
    errs = 0;
    st = hrs_start();
}

static void draw(void) {
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    aafont_line(&aafont_small, 3, "BATTITO", C_YELLOW, C_BLACK);
    if (st != HRS_ON) {
        draw_error();
        return;
    }
    draw_text();
    lcd_fill(0, WAVE_Y - 2, lcd.width, 1, C_GREY);
    lcd_fill(0, WAVE_Y + WAVE_H + 1, lcd.width, 1, C_GREY);
    aafont_line(&aafont_small, 143, "TIENI 3S ESCI", C_GREY, C_BLACK);
    t_sample = t_text = clock_time();
}

/* one column of the sweeping wave: AC around its own average, scaled to its recent swing */
static void plot(s32 ac, int peak) {
    static s32 swing = 16;
    s32 v = ac - mid8 / 8, a = v < 0 ? -v : v;
    int y, y0, y1;
    swing -= swing / 64;
    if (a > swing) swing = a;
    if (swing < 8) swing = 8;
    y = WAVE_Y + WAVE_H / 2 - (int)(v * (WAVE_H / 2 - 1) / swing);
    if (y < WAVE_Y) y = WAVE_Y;
    if (y >= WAVE_Y + WAVE_H) y = WAVE_Y + WAVE_H - 1;
    y0 = prev_y < y ? prev_y : y;
    y1 = prev_y < y ? y : prev_y;
    lcd_fill(x, WAVE_Y, 1, WAVE_H, C_BLACK);
    if (x + 1 < lcd.width) lcd_fill(x + 1, WAVE_Y, 1, WAVE_H, C_BLACK);  /* the gap ahead */
    if (peak) lcd_fill(x, WAVE_Y, 1, 4, C_RED);
    lcd_fill(x, y0, 1, y1 - y0 + 1, C_GREEN);
    prev_y = y;
    x = (x + 1) % lcd.width;
}

static void sample(void) {
    s32 p, s, thr, a;
    int peak = 0, i;
    if (!hrs_read(&c0, &c1)) {
        errs++;
        return;
    }
    t_now_ms += SAMPLE_MS;
    if (!dc16) dc16 = (s32)c0 * 16;
    dc16 += ((s32)c0 - dc16 / 16) / 2;           /* DC: 1/32 of each sample (~1.6 s) */
    p = dc16 / 16 - (s32)c0;                     /* AC, a beat is a rise */
    s4 += p - s4 / 4;                            /* light smoothing */
    s = s4 / 4;
    mid8 += s - mid8 / 8;                        /* AC's own average, to centre the wave */
    slope = s - s_hist[1];                       /* rise over 2 samples = 100 ms */
    s_hist[1] = s_hist[0];
    s_hist[0] = s;
    env -= env / 64;                             /* envelope of the rising slopes */
    a = slope > 0 ? slope : 0;
    if (a > env) env = a;
    thr = env / 2;
    if (!above && slope > thr && thr > 2 && t_now_ms - t_peak_ms >= 300) {
        above = peak = 1;
        beats++;
        if (t_peak_ms) {
            u32 d = t_now_ms - t_peak_ms;
            if (d >= 300 && d <= 1500) {
                u32 sum = 0;
                ibi[n_ibi % IBI_N] = d;
                n_ibi++;
                for (i = 0; i < IBI_N && i < n_ibi; i++) sum += ibi[i];
                bpm = 60000 * i / sum;
            }
        }
        t_peak_ms = t_now_ms;
    } else if (above && slope < thr / 4) {
        above = 0;
    }
    plot(s, peak);
}

static void tick(void) {
    if (st != HRS_ON) return;
    hrs_keep();
    if (mos_ms_since(t_sample) >= SAMPLE_MS) {
        t_sample += SAMPLE_MS * sys_tick_per_us * 1000;
        if (mos_ms_since(t_sample) > 500) t_sample = clock_time();   /* fell behind */
        sample();
    }
    if (mos_ms_since(t_text) >= 500) {
        t_text = clock_time();
        draw_text();
    }
}

static void key(int ev) {
    if (hrs_state() != HRS_ON) st = hrs_start();
    reset_signal();
    mos_redraw();
}

const mos_app_t app_heart = {
    "heart", MOS_F_HOLD_EXIT, 120, enter, draw, tick, key, 0, 0, 0, 0, 0,
};
