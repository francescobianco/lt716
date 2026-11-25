/* MorseOS Bluetooth scan, see blescan.h. */
#include "blescan.h"
#include "drivers/5316/clock.h"
#include "stack/ble/ble_common.h"
#include "stack/ble/hci/hci_const.h"
#include "stack/ble/ll/ll_whitelist.h"   /* SCAN_FP_ALLOW_ADV_ANY */

/* liblt_5316.a, ll_scan.o / hci.o: disassembled 2026-11-25, all plain stores (callbacks, a flag
 * bit in blts.scan_extension_mask, the handler pointer, the event mask). Declared here because
 * ll_scan.h defines (not declares) blts and blts_scan_interval. */
typedef int (*hci_handler_t)(u32 h, u8 *para, int n);
void blc_ll_initScanning_module(u8 *public_adr);
ble_sts_t blc_ll_setScanParameter(u8 scan_type, u16 scan_interval, u16 scan_window,
                                  u8 ownAddrType, u8 filter_policy);
ble_sts_t blc_ll_addScanningInAdvState(void);
ble_sts_t blc_ll_removeScanningFromAdvState(void);
ble_sts_t blc_ll_addScanningInConnSlaveRole(void);
ble_sts_t blc_ll_removeScanningFromConnSLaveRole(void);
void blc_hci_registerControllerEventHandler(hci_handler_t handler);
ble_sts_t blc_hci_le_setEventMask_cmd(u32 evtMask);

extern u8 mac_public[6];            /* core/system/main.c (not used by the library) */

static volatile blescan_dev_t devs[BLESCAN_MAX];
static volatile u8 ndev;
static u32 t_until;                 /* clock_time() until which the scan is wanted */
static u8 wanted, active, inited;

static u32 ms_since(u32 t) { return (clock_time() - t) / (sys_tick_per_us * 1000); }

/* in the radio interrupt: find or make the slot, copy MAC, RSSI and the name if there is one */
static int on_hci(u32 h, u8 *para, int n) {
    event_adv_report_t *p = (event_adv_report_t *)para;
    int i, slot = -1, len;
    u32 oldest = 0;
    if ((h & 0xff) != HCI_EVT_LE_META || p->subcode != HCI_SUB_EVT_LE_ADVERTISING_REPORT) return 0;
    len = p->len > 31 ? 31 : p->len;
    for (i = 0; i < ndev; i++) {
        int k, same = devs[i].addr_type == p->adr_type;
        for (k = 0; k < 6 && same; k++) same = devs[i].mac[k] == p->mac[k];
        if (same) { slot = i; break; }
    }
    if (slot < 0) {
        if (ndev < BLESCAN_MAX) {
            slot = ndev++;
        } else {                     /* full: the one not heard for the longest */
            for (i = 0; i < BLESCAN_MAX; i++)
                if (clock_time() - devs[i].seen >= oldest) oldest = clock_time() - devs[i].seen, slot = i;
        }
        for (i = 0; i < 6; i++) devs[slot].mac[i] = p->mac[i];
        devs[slot].addr_type = p->adr_type;
        devs[slot].name[0] = 0;
    }
    devs[slot].rssi = (s8)p->data[len];
    devs[slot].seen = clock_time();
    for (i = 0; i + 1 < len && p->data[i];) {   /* AD structures: <len> <type> <data...> */
        int l = p->data[i], t = p->data[i + 1];
        if ((t == 0x08 || t == 0x09) && i + 1 + l <= len) {  /* shortened / complete name */
            int k, m = l - 1 > BLESCAN_NAME ? BLESCAN_NAME : l - 1;
            for (k = 0; k < m; k++) {
                char c = p->data[i + 2 + k];
                devs[slot].name[k] = c >= 32 && c < 127 ? c : '?';
            }
            devs[slot].name[m] = 0;
            break;
        }
        i += l + 1;
    }
    return 0;
}

static void start(void) {
    if (!inited) {
        inited = 1;
        blc_ll_initScanning_module(mac_public);
    }
    blc_ll_setScanParameter(SCAN_TYPE_PASSIVE, SCAN_INTERVAL_100MS, SCAN_INTERVAL_100MS,
                            OWN_ADDRESS_PUBLIC, SCAN_FP_ALLOW_ADV_ANY);
    blc_hci_registerControllerEventHandler(on_hci);
    blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_ADVERTISING_REPORT);
    blc_ll_addScanningInAdvState();
    blc_ll_addScanningInConnSlaveRole();
    active = 1;
}

static void stop(void) {
    blc_ll_removeScanningFromAdvState();
    blc_ll_removeScanningFromConnSLaveRole();
    blc_hci_le_setEventMask_cmd(0);
    blc_hci_registerControllerEventHandler(0);
    active = 0;
}

void blescan_request(u32 ms) {  /* ms up to 2 min: the 32-bit tick wraps at 268 s */
    u32 until = clock_time() + ms * sys_tick_per_us * 1000;
    if (!wanted || (s32)(until - t_until) > 0) t_until = until;
    wanted = 1;
}

void blescan_stop(void) { wanted = 0; }

void blescan_poll(void) {
    if (wanted && (s32)(clock_time() - t_until) >= 0) wanted = 0;
    if (wanted && !active) start();
    else if (!wanted && active) stop();
}

int blescan_active(void) { return active; }
void blescan_clear(void) { ndev = 0; }
const blescan_dev_t *blescan_dev(int i) { return (const blescan_dev_t *)&devs[i]; }
u32 blescan_age_s(const blescan_dev_t *d) { return ms_since(d->seen) / 1000; }

int blescan_sorted(u8 idx[BLESCAN_MAX]) {
    int n = ndev, i, j;
    for (i = 0; i < n; i++) idx[i] = i;
    for (i = 1; i < n; i++)                 /* insertion sort, strongest first */
        for (j = i; j > 0 && devs[idx[j]].rssi > devs[idx[j - 1]].rssi; j--) {
            u8 t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    return n;
}
