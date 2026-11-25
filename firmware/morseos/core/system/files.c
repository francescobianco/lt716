/* MorseOS files: the table, the readers/writers, the battery log. See files.h. */
#include "files.h"
#include "mos.h"
#include "morse.h"
#include "st7735.h"
#include "drivers/5316/register.h"
#include "drivers/5316/timer.h"
#ifdef MOS_HAVE_BATTERY
#include "battery.h"
#endif
#ifdef MOS_HAVE_RTC
#include "rtc.h"
#endif

#define WIN       16    /* data bytes per notification */
#define WBUF      64
#define LOG_N     48    /* battery samples kept: 24 h at the default 30 min */

/* from main.c */
extern u8 sg_boot, sg_healthy, sg_rearms;
extern volatile u8 reboot_pending;
extern u32 stack_free(void);

/* ------------------------------------------------------------------ output window
 * A reader prints the whole file every time; only the bytes in [win_lo, win_lo + WIN) are
 * kept. So no buffer for the file, no size limit, a few bytes of RAM. */
static u32 pos, win_lo;
static u8 win[WIN];

static void putc_(char c) {
    if (pos >= win_lo && pos < win_lo + WIN) win[pos - win_lo] = c;
    pos++;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putn(u32 v, int width) {
    char t[12];
    mos_num(t, v, width);
    puts_(t);
}
static void putsn(int v) {  /* signed */
    if (v < 0) putc_('-'), v = -v;
    putn(v, 1);
}
static void nl(void) { putc_('\n'); }

/* ------------------------------------------------------------------ parsing */

static const char *num(const char *s, int *v) {  /* digits -> v; 0 if none */
    int n = 0, any = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0'), any = 1;
    *v = n;
    return any ? s : 0;
}

static int one_num(const char *s, int lo, int hi, int *v) {  /* the whole value is a number */
    s = num(s, v);
    return s && !*s && *v >= lo && *v <= hi;
}

/* ------------------------------------------------------------------ battery log */

typedef struct {       /* 8 bytes: RAM is short (the stack lives above the bss) */
    u16 up;            /* uptime, minutes */
    u16 mv;
    u8 day;            /* band clock at the sample; bit 7 = the clock was set */
    u8 hour, min;
    u8 pct;            /* bit 7 = charging */
} sample_t;

static sample_t log_s[LOG_N];
static int log_n, log_head;     /* log_head = next slot */
static int log_interval = 30;   /* minutes, 0 = off */
static u32 log_next = 60;       /* uptime of the next sample: the first one a minute after boot */

static void log_sample(void) {
#ifdef MOS_HAVE_BATTERY
    sample_t *s = &log_s[log_head];
    battery_update();
    s->up = rtc_uptime() / 60;
    s->mv = battery_mv();
    s->pct = (battery_level() < 0 ? 0 : battery_level()) | (battery_charging() ? 0x80 : 0);
    s->day = 0;
#ifdef MOS_HAVE_RTC
    if (rtc_is_set()) {
        rtc_time_t t;
        rtc_get(&t);
        s->day = t.day | 0x80;
        s->hour = t.hour;
        s->min = t.min;
    }
#endif
    log_head = (log_head + 1) % LOG_N;
    if (log_n < LOG_N) log_n++;
#endif
}

void files_poll(void) {
    u32 up = rtc_uptime();
    if (log_interval && up >= log_next) {
        log_next = up + 60 * (u32)log_interval;
        log_sample();
    }
}

/* ------------------------------------------------------------------ the files */

static void r_version(void) { puts_("MorseOS " MOS_VERSION); nl(); }
static void r_uptime(void) { putn(rtc_uptime(), 1); nl(); }

static void r_band(void) {
    if (lcd_board) {
        putn(lcd_board->band, 1);
        putc_(' ');
        puts_(lcd_board->name);
    } else {
        puts_("?");
    }
    nl();
}

static void r_safeguard(void) {
    puts_("boot ");
    putn(sg_boot, 1);
    puts_("\nhealthy ");
    putn(sg_healthy, 1);
    puts_("\nwatchdog ");
    puts_(reg_tmr_ctrl & FLD_TMR_WD_EN ? "on" : "off");
    puts_("\nrearms ");
    putn(sg_rearms, 1);
    nl();
}

static void r_stack(void) { putn(stack_free(), 1); nl(); }

static int w_reboot(const char *s) {
    if (!(s[0] == '1' && !s[1])) return 3;
    reboot_pending = 1;
    return 0;
}

static void r_apps(void) {  /* "<menu> <label>" per app, like a tiny /proc/modules */
    int i;
    for (i = 0; i < mos_entry_count; i++) {
        puts_(mos_entries[i].menu[0] ? mos_entries[i].menu : "-");
        putc_(' ');
        puts_(mos_entries[i].label);
        nl();
    }
}

#ifdef MOS_HAVE_BATTERY
static void r_voltage(void) { battery_update(); putn(battery_mv(), 1); nl(); }
static void r_capacity(void) { battery_update(); putn(battery_level() < 0 ? 0 : battery_level(), 1); nl(); }
static void r_status(void) {  /* the words of /sys/class/power_supply/.../status */
    battery_update();
    puts_(battery_full() ? "Full" : battery_charging() ? "Charging" : "Discharging");
    nl();
}

static void r_log(void) {
    int i;
    puts_("uptime_min,time,mv,pct,charging\n");
    for (i = 0; i < log_n; i++) {
        const sample_t *s = &log_s[(log_head - log_n + i + LOG_N) % LOG_N];
        putn(s->up, 1);
        putc_(',');
        if (s->day & 0x80) {
            putn(s->day & 0x7f, 2);
            putc_(' ');
            putn(s->hour, 2);
            putc_(':');
            putn(s->min, 2);
        }
        putc_(',');
        putn(s->mv, 1);
        putc_(',');
        putn(s->pct & 0x7f, 1);
        putc_(',');
        putn(s->pct >> 7, 1);
        nl();
    }
}

static int w_log(const char *s) {  /* any write empties it, like "> file" */
    log_n = log_head = 0;
    return 0;
}

static void r_interval(void) { putn(log_interval, 1); nl(); }
static int w_interval(const char *s) {
    int v;
    if (!one_num(s, 0, 1440, &v)) return 3;
    log_interval = v;
    log_next = rtc_uptime() + 60 * (u32)v;
    return 0;
}
#endif

#ifdef MOS_HAVE_RTC
static void r_now(void) {
    rtc_time_t t;
    if (!rtc_is_set()) {
        puts_("unset\n");
        return;
    }
    rtc_get(&t);
    putn(t.year, 4); putc_('-'); putn(t.month, 2); putc_('-'); putn(t.day, 2);
    putc_(' ');
    putn(t.hour, 2); putc_(':'); putn(t.min, 2); putc_(':'); putn(t.sec, 2);
    nl();
}

/* "YYYY-MM-DD HH:MM:SS" (or "YYYY-MM-DD HH:MM") */
static int w_now(const char *s) {
    int y, mo, d, h, mi, se = 0;
    if (!(s = num(s, &y)) || *s++ != '-' || !(s = num(s, &mo)) || *s++ != '-' ||
        !(s = num(s, &d)) || (*s != ' ' && *s != 'T') || !(s = num(s + 1, &h)) ||
        *s++ != ':' || !(s = num(s, &mi)))
        return 3;
    if (*s == ':' && !(s = num(s + 1, &se))) return 3;
    if (*s || y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 ||
        mi > 59 || se > 59)
        return 3;
    rtc_set(y, mo, d, h, mi, se);
    return 0;
}
#endif

static void r_speed(void) { putn(morse_speed(), 1); nl(); }
static int w_speed(const char *s) {
    int v;
    if (!one_num(s, 1, 16, &v)) return 3;
    morse_set_speed(v);
    return 0;
}
static void r_threshold(void) { putn(morse_threshold_ms(), 1); nl(); }
static void r_reset_ms(void) { putn(morse_reset_ms(), 1); nl(); }

static void r_vibration(void) { putn(mos_vibration(), 1); nl(); }
static int w_vibration(const char *s) {
    int v;
    if (!one_num(s, 0, 1, &v)) return 3;
    mos_set_vibration(v);
    return 0;
}

static void r_connected(void) { putn(mos_connected(), 1); nl(); }
static void r_peer(void) { puts_(mos_peer()); nl(); }
static void r_rssi(void) { putsn(mos_rssi()); nl(); }

typedef struct {
    const char *path;
    void (*read)(void);
    int (*write)(const char *s);  /* 0 = read only */
} file_t;

static const file_t files[] = {
    {"/sys/version", r_version, 0},
    {"/sys/uptime", r_uptime, 0},
    {"/sys/band", r_band, 0},
    {"/sys/safeguard", r_safeguard, 0},
    {"/sys/apps", r_apps, 0},
    {"/sys/stack_free", r_stack, 0},
    {"/sys/reboot", 0, w_reboot},
#ifdef MOS_HAVE_RTC
    {"/clock/now", r_now, w_now},
#endif
#ifdef MOS_HAVE_BATTERY
    {"/power/voltage_mv", r_voltage, 0},
    {"/power/capacity", r_capacity, 0},
    {"/power/status", r_status, 0},
    {"/log/battery.csv", r_log, w_log},
    {"/log/battery.interval", r_interval, w_interval},
#endif
    {"/morse/speed", r_speed, w_speed},
    {"/morse/dot_ms", r_threshold, 0},
    {"/morse/reset_ms", r_reset_ms, 0},
    {"/ui/vibration", r_vibration, w_vibration},
    {"/bt/connected", r_connected, 0},
    {"/bt/peer", r_peer, 0},
    {"/bt/rssi", r_rssi, 0},
};
#define N_FILES ((int)(sizeof(files) / sizeof(files[0])))

static void r_list(void) {  /* file 0 */
    int i;
    for (i = 0; i < N_FILES; i++) {
        putc_(files[i].read ? 'r' : '-');
        putc_(files[i].write ? 'w' : '-');
        putc_(' ');
        puts_(files[i].path);
        nl();
    }
}

/* ------------------------------------------------------------------ requests */

static volatile u8 rd_on, rd_id, wr_pending, wr_id, wr_len;
static volatile u16 rd_off;
static u8 wbuf[WBUF];
static u8 status_pkt[5], status_pending;

int files_cmd(const u8 *d, int len) {
    int i;
    switch (d[0]) {
    case 0x40:
        if (len < 4) return 1;
        rd_id = d[1];
        rd_off = d[2] | d[3] << 8;
        rd_on = 1;
        return 1;
    case 0x41:
        if (len < 3) return 1;
        for (i = 0; i < len - 3 && d[2] + i < WBUF; i++) wbuf[d[2] + i] = d[3 + i];
        return 1;
    case 0x42:
        if (len < 3 || wr_pending) return 1;
        wr_id = d[1];
        wr_len = d[2] < WBUF ? d[2] : WBUF - 1;
        wr_pending = 1;
        return 1;
    }
    return 0;
}

static void do_write(void) {
    char *s = (char *)wbuf;  /* parsed in place */
    int id = wr_id, n = wr_len, st;
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) n--;  /* echo's \n */
    s[n] = 0;
    if (id < 1 || id > N_FILES) st = 1;
    else if (!files[id - 1].write) st = 2;
    else st = files[id - 1].write(s);
    status_pkt[0] = 0xFF;
    status_pkt[1] = 0xFF;
    status_pkt[2] = id;
    status_pkt[3] = 0xFC;
    status_pkt[4] = st;
    status_pending = 1;
    wr_pending = 0;
}

void files_pump(int (*tx)(const u8 *p, int n)) {
    int burst;
    if (wr_pending && !status_pending) do_write();
    if (status_pending) {
        if (tx(status_pkt, 5)) return;
        status_pending = 0;
    }
    for (burst = 0; burst < 8 && rd_on; burst++) {
        u8 pkt[4 + WIN];
        int id = rd_id, n, i;
        void (*rd)(void) = id == 0 ? r_list : id <= N_FILES ? files[id - 1].read : 0;
        pos = 0;
        win_lo = rd_off;
        if (rd) rd();                       /* the whole file, keeping only this window */
        n = pos > win_lo ? pos - win_lo : 0;
        if (n > WIN) n = WIN;
        pkt[0] = win_lo;
        pkt[1] = win_lo >> 8;
        pkt[2] = id;
        pkt[3] = 0xFC;
        for (i = 0; i < n; i++) pkt[4 + i] = win[i];
        if (tx(pkt, 4 + n)) return;         /* TX FIFO full: again on the next pass */
        if (n < WIN) rd_on = 0;             /* short packet: end of file */
        else rd_off = win_lo + WIN;
    }
}
