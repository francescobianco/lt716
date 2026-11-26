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
#ifdef MOS_HAVE_BLESCAN
#include "blescan.h"
#endif
#ifdef MOS_HAVE_ALARMS
#include "alarms.h"
#endif
#ifdef MOS_HAVE_LINKLOG
#include "linklog.h"
#endif
#ifdef MOS_HAVE_HRS3300
#include "hrs3300.h"
#endif

#define WIN       16    /* data bytes per notification */
#define WBUF      40    /* one line written: the longest is an alarm (33) */
#define LOG_N     24    /* battery samples kept: 12 h at the default 30 min (0.8.8: was 48) */

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

/* 4 bytes a sample (RAM is short: the stack lives above the bss): the uptime in minutes and
 * the voltage; the time of day comes from the clock at read time, the percent from the curve */
static u16 log_up[LOG_N];       /* uptime, minutes (wraps after 45 days) */
static u16 log_mv[LOG_N];       /* bit 15 = charging */
static u8 log_n, log_head;      /* log_head = next slot */
static u16 log_interval = 30;   /* minutes, 0 = off */
static u32 log_next = 60;       /* uptime of the next sample: the first one a minute after boot */

static void log_sample(void) {
#ifdef MOS_HAVE_BATTERY
    battery_update();
    log_up[log_head] = rtc_uptime() / 60;
    log_mv[log_head] = battery_mv() | (battery_charging() ? 0x8000 : 0);
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
    u32 now = rtc_uptime();
    puts_("uptime_min,time,mv,pct,charging\n");
    for (i = 0; i < log_n; i++) {
        int k = (log_head - log_n + i + LOG_N) % LOG_N;
        u32 mv = log_mv[k] & 0x7fff, chg = log_mv[k] >> 15;
        putn(log_up[k], 1);
        putc_(',');
#ifdef MOS_HAVE_RTC
        if (rtc_is_set()) {
            rtc_time_t t;
            u32 at = (u32)log_up[k] * 60;
            rtc_get_ago(now > at ? now - at : 0, &t);
            putn(t.day, 2);
            putc_(' ');
            putn(t.hour, 2);
            putc_(':');
            putn(t.min, 2);
        }
#endif
        putc_(',');
        putn(mv, 1);
        putc_(',');
        putn(battery_pct(mv, chg, 0), 1);
        putc_(',');
        putn(chg, 1);
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

/* the gap that ends a letter: a number of ms fixes it, "auto" = 1.5 x dot_ms (follows speed) */
static void r_letter(void) {
    putn(morse_letter_ms(), 1);
    if (!morse_letter_fixed()) puts_(" auto");
    nl();
}
static int w_letter(const char *s) {
    int v;
    if (s[0] == 'a' && s[1] == 'u' && s[2] == 't' && s[3] == 'o' && !s[4]) v = 0;
    else if (!one_num(s, 150, 3000, &v)) return 3;
    morse_set_letter_ms(v);
    return 0;
}

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

#ifdef MOS_HAVE_BLESCAN
/* /bt/scan: 1 while scanning; write N to scan for N seconds (1..120), 0 to stop */
static void r_scan(void) { putn(blescan_active(), 1); nl(); }
static int w_scan(const char *s) {
    int v;
    if (!one_num(s, 0, 120, &v)) return 3;
    if (v) blescan_request(v * 1000);
    else blescan_stop();
    return 0;
}

static void puthex(int v) {
    putc_("0123456789ABCDEF"[v >> 4 & 15]);
    putc_("0123456789ABCDEF"[v & 15]);
}

/* /bt/devices: what the scan heard, strongest first; write anything to clear it */
static void r_devices(void) {
    u8 idx[BLESCAN_MAX];
    int n = blescan_sorted(idx), i, k;
    puts_("mac,type,rssi,age_s,name\n");
    for (i = 0; i < n; i++) {
        const blescan_dev_t *d = blescan_dev(idx[i]);
        for (k = 5; k >= 0; k--) {
            puthex(d->mac[k]);
            if (k) putc_(':');
        }
        puts_(d->addr_type ? ",random," : ",public,");
        putsn(d->rssi);
        putc_(',');
        putn(blescan_age_s(d), 1);
        putc_(',');
        puts_(d->name);
        nl();
    }
}
static int w_devices(const char *s) { blescan_clear(); return 0; }
#endif

#ifdef MOS_HAVE_ALARMS
/* /clock/alarms: "HH:MM DAYS" per line; write a line to add or change one, "-HH:MM" to remove
 * it, nothing (": > alarms") to clear them all */
static void r_alarms(void) {
    int i;
    char d[28];
    for (i = 0; i < alarms_count(); i++) {
        const alarm_t *a = alarms_get(i);
        putn(a->hour, 2);
        putc_(':');
        putn(a->min, 2);
        putc_(' ');
        alarms_days_text(a->days, d);
        puts_(d);
        nl();
    }
}
static int w_alarms(const char *s) { return alarms_line(s) ? 3 : 0; }
#endif

#ifdef MOS_HAVE_LINKLOG
static void putm(u32 cm) {  /* metres with 2 decimals */
    putn(cm / 100, 1);
    putc_('.');
    putn(cm % 100, 2);
}

/* /bt/link.csv: connections, losses (reason in hex: 08 = out of range, 13 = closed by the PC)
 * and an RSSI sample every /bt/link.interval s with the distance it suggests; write = clear */
static void r_link(void) {
    int i;
    puts_("uptime_s,event,rssi,dist_m\n");
    for (i = 0; i < linklog_count(); i++) {
        const linklog_ev_t *e = linklog_get(i);
        putn((u32)e->t2 * 2, 1);
        if (e->kind == LL_SAMPLE) {
            puts_(",sample,");
            putsn(e->v);
            putc_(',');
            putm(linklog_distance_cm(e->v));
        } else if (e->kind == LL_CONNECT) {
            puts_(",connect,,");
        } else {
            puts_(",lost-");
            putc_("0123456789abcdef"[(u8)e->v >> 4]);
            putc_("0123456789abcdef"[e->v & 15]);
            puts_(",,");
        }
        nl();
    }
}
static int w_link(const char *s) { linklog_clear(); return 0; }

static void r_distance(void) {   /* estimate now, from the averaged RSSI */
    if (linklog_rssi() > -127) putm(linklog_distance_cm(linklog_rssi()));
    nl();
}
static void r_range(void) {      /* last out-of-range loss: "dBm metres", empty if none yet */
    if (linklog_range()) {
        putsn(linklog_range());
        putc_(' ');
        putm(linklog_distance_cm(linklog_range()));
    }
    nl();
}
static void r_rssi_1m(void) { putsn(linklog_rssi_1m()); nl(); }
static int w_rssi_1m(const char *s) {
    int v;
    if (*s == '-') s++;
    if (!one_num(s, 20, 100, &v)) return 3;
    linklog_set_rssi_1m(-v);
    return 0;
}
static void r_n10(void) { putn(linklog_n10(), 1); nl(); }
static int w_n10(const char *s) {
    int v;
    if (!one_num(s, 15, 50, &v)) return 3;
    linklog_set_n10(v);
    return 0;
}
static void r_linkint(void) { putn(linklog_interval(), 1); nl(); }
static int w_linkint(const char *s) {
    int v;
    if (!one_num(s, 0, 600, &v)) return 3;
    linklog_set_interval(v);
    return 0;
}
#endif

#ifdef MOS_HAVE_HRS3300
/* /health/hr_pins: "SDA SCL" of the sensor's software I2C, e.g. "A4 C7" (only pins that are
 * not the display, motor, key, battery or SPI ones; not while BATTITO is open) */
static void r_hrpins(void) {
    char t[6];
    hrs_pins_text(t);
    puts_(t);
    nl();
}
static int w_hrpins(const char *s) { return hrs_set_pins(s) ? 3 : 0; }
static void r_hrpower(void) {
    char t[4];
    hrs_power_text(t);
    puts_(t);
    nl();
}
static int w_hrpower(const char *s) { return hrs_set_power(s) ? 3 : 0; }
static void r_hrscan(void) { hrs_scan_text(puts_); }
static int w_hrscan(const char *s) {
    if (!(s[0] == '1' && !s[1])) return 3;
    hrs_scan();
    return 0;
}
static void r_hrstate(void) {   /* off, on, lines-low, no-answer, id-XX */
    static const char *const n[] = {"off", "on", "lines-low", "no-answer", "id-"};
    int s = hrs_state();
    puts_(n[s]);
    if (s == HRS_ERR_ID) {
        putc_("0123456789abcdef"[hrs_id() >> 4]);
        putc_("0123456789abcdef"[hrs_id() & 15]);
    }
    nl();
}
#endif

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
#ifdef MOS_HAVE_ALARMS
    {"/clock/alarms", r_alarms, w_alarms},
#endif
#endif
#ifdef MOS_HAVE_BATTERY
    {"/power/voltage_mv", r_voltage, 0},
    {"/power/capacity", r_capacity, 0},
    {"/power/status", r_status, 0},
    {"/log/battery.csv", r_log, w_log},
    {"/log/battery.interval", r_interval, w_interval},
#endif
#ifdef MOS_HAVE_HRS3300
    {"/health/hr_pins", r_hrpins, w_hrpins},
    {"/health/hr_state", r_hrstate, 0},
    {"/health/hr_power", r_hrpower, w_hrpower},
    {"/health/hr_scan", r_hrscan, w_hrscan},
#endif
    {"/morse/speed", r_speed, w_speed},
    {"/morse/dot_ms", r_threshold, 0},
    {"/morse/reset_ms", r_reset_ms, 0},
    {"/morse/letter_ms", r_letter, w_letter},
    {"/ui/vibration", r_vibration, w_vibration},
    {"/bt/connected", r_connected, 0},
    {"/bt/peer", r_peer, 0},
    {"/bt/rssi", r_rssi, 0},
#ifdef MOS_HAVE_LINKLOG
    {"/bt/distance", r_distance, 0},
    {"/bt/range", r_range, 0},
    {"/bt/link.csv", r_link, w_link},
    {"/bt/link.interval", r_linkint, w_linkint},
    {"/bt/rssi_1m", r_rssi_1m, w_rssi_1m},
    {"/bt/path_n10", r_n10, w_n10},
#endif
#ifdef MOS_HAVE_BLESCAN
    {"/bt/scan", r_scan, w_scan},
    {"/bt/devices", r_devices, w_devices},
#endif
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
