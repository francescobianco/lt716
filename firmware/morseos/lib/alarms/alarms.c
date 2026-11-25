/* MorseOS alarms, see alarms.h. */
#include "alarms.h"
#include "rtc.h"

static alarm_t al[ALARMS_MAX];
static int n;
static int last_minute = -1;  /* minute of the week already checked */
static const char *const names[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};

int alarms_count(void) { return n; }
const alarm_t *alarms_get(int i) { return &al[i]; }
void alarms_clear(void) { n = 0; }

static int find(int h, int m) {
    int i;
    for (i = 0; i < n; i++)
        if (al[i].hour == h && al[i].min == m) return i;
    return -1;
}

int alarms_set(int h, int m, int days) {
    int i = find(h, m);
    if (i >= 0) {
        al[i].days = days;
        return 0;
    }
    if (n == ALARMS_MAX) return -1;
    for (i = n; i > 0 && al[i - 1].hour * 60 + al[i - 1].min > h * 60 + m; i--) al[i] = al[i - 1];
    al[i].hour = h;
    al[i].min = m;
    al[i].days = days;
    n++;
    return 0;
}

int alarms_del(int h, int m) {
    int i = find(h, m);
    if (i < 0) return -1;
    for (n--; i < n; i++) al[i] = al[i + 1];
    return 0;
}

static const char *two(const char *s, int *v) {
    if (s[0] < '0' || s[0] > '9') return 0;
    *v = s[0] - '0';
    if (s[1] >= '0' && s[1] <= '9') return *v = *v * 10 + s[1] - '0', s + 2;
    return s + 1;
}

int alarms_line(const char *s) {
    int h, m, days = 0, del = 0, any = 0, i;
    while (*s == ' ') s++;
    if (!*s) {
        alarms_clear();
        return 0;
    }
    if (*s == '-') del = 1, s++;
    if (!(s = two(s, &h)) || *s++ != ':' || !(s = two(s, &m)) || h > 23 || m > 59) return -1;
    while (*s) {
        char w[6];
        int k = 0;
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        while (*s && *s != ' ' && *s != ',' && k < 5) {
            char c = *s++;
            w[k++] = c >= 'a' && c <= 'z' ? c - 32 : c;
        }
        w[k] = 0;
        any = 1;
        if (w[0] == 'D' && w[1] == 'A' && w[2] == 'I' && w[3] == 'L' && w[4] == 'Y') {
            days = ALARMS_DAILY;
            continue;
        }
        if (w[0] == 'O' && w[1] == 'N' && w[2] == 'C' && w[3] == 'E' && !w[4]) continue;
        for (i = 0; i < 7; i++)
            if (w[0] == names[i][0] && w[1] == names[i][1] && w[2] == names[i][2] && !w[3]) break;
        if (i == 7) return -1;
        days |= 1 << i;
    }
    if (del) return alarms_del(h, m) ? -1 : 0;
    if (!any) days = ALARMS_DAILY;   /* just a time: every day */
    return alarms_set(h, m, days) ? -2 : 0;
}

void alarms_days_text(int days, char *out) {
    int i;
    const char *w = days == ALARMS_DAILY ? "DAILY" : !days ? "ONCE" : 0;
    if (w) {
        while ((*out++ = *w++));
        return;
    }
    for (i = 0; i < 7; i++)
        if (days >> i & 1) {
            *out++ = names[i][0];
            *out++ = names[i][1];
            *out++ = names[i][2];
            *out++ = ' ';
        }
    out[-1] = 0;
}

void alarms_from_fitpro(const u8 *d, int len) {
    int i;
    alarms_clear();
    for (i = 0; i + 5 <= len; i += 5) {
        /* bits 39..0: year 34, month 30, day 25, hour 20, min 14, on 11, days 0..6 */
        u32 lo = (u32)d[i + 1] << 24 | d[i + 2] << 16 | d[i + 3] << 8 | d[i + 4];
        int h = lo >> 20 & 0x1F, m = lo >> 14 & 0x3F;
        if (!(lo >> 11 & 1) || h > 23 || m > 59) continue;
        alarms_set(h, m, lo & 0x7F);
    }
}

int alarms_to_fitpro(u8 *out) {
    int i, k = 0;
    rtc_time_t t;
    rtc_get(&t);
    for (i = 0; i < n; i++) {
        u32 hi = (u32)(t.year - 2000) << 2 | t.month >> 2;   /* bits 39..32 */
        u32 lo = (u32)t.month << 30 | (u32)t.day << 25 | (u32)al[i].hour << 20 |
                 (u32)al[i].min << 14 | 1u << 11 | al[i].days;
        out[k++] = hi;
        out[k++] = lo >> 24;
        out[k++] = lo >> 16;
        out[k++] = lo >> 8;
        out[k++] = lo;
    }
    return k;
}

int alarms_due(void) {
    rtc_time_t t;
    int i, minute, bit;
    if (!rtc_is_set() || !n) return 0;
    rtc_get(&t);
    minute = (t.wday * 24 + t.hour) * 60 + t.min;
    if (minute == last_minute) return 0;
    last_minute = minute;
    bit = 1 << ((t.wday + 6) % 7);   /* rtc: 0 = Sunday; alarms: bit 0 = Monday */
    for (i = 0; i < n; i++)
        if (al[i].hour == t.hour && al[i].min == t.min && (!al[i].days || al[i].days & bit)) {
            if (!al[i].days) alarms_del(t.hour, t.min);   /* once */
            return 1;
        }
    return 0;
}
