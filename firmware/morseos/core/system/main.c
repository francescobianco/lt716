/*
 * MorseOS for the LT716 (Telink TLSR8232): core/system module. BLE, GATT, flash dumper
 * commands, the two ways back to the vendor updater, and the main loop. What else is in the
 * firmware (apps, libraries) is decided by bundle.ini, see tools/bundle.py.
 *
 * Built on the flash dumper / display test firmware: same BLE services and commands
 * (dump, info, lcd, updater), same two ways back to the vendor updater:
 *   - BLE command 05 'UPDT'
 *   - touch key held 20 s (warning at 10 s, then three short buzzes), no BLE needed
 * The UI framework is core/ui (mos.c); apps are in apps/, libraries in lib/.
 */

#include "drivers/5316/bsp.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/compiler.h"
#include "drivers/5316/driver_5316.h"
#include "drivers/5316/flash.h"
#include "drivers/5316/gpio.h"
#include "drivers/5316/register.h"
#include "drivers/5316/timer.h"
#include "stack/ble/ble_common.h"
#include "stack/ble/ble_smp.h"
#include "stack/ble/ll/ll.h"
#include "stack/ble/service/ble_ll_ota.h"
#include "stack/ble/att.h"
#include "vendor/common/blt_common.h"
#include "st7735.h"
#include "mos.h"
#ifdef MOS_HAVE_FITPRO
#include "fitpro.h"
#endif
#ifdef MOS_HAVE_BATTERY
#include "battery.h"
#endif
#ifdef MOS_HAVE_RTC
#include "rtc.h"
#endif
#ifdef MOS_HAVE_MESSAGE
#include "message.h"
#endif

#define DUMPER_VERSION 4   /* 4 = MorseOS 0.1 */
#define ASSET_START    0x29000
#define ASSET_END      0x70000
#define FLASH_SIZE     0x80000
#define PIN_MOTOR      GPIO_PA5  /* active high, plain GPIO (docs/hardware.md) */
#define PIN_TOUCH      GPIO_PC2  /* active high while touched */
#define EMERGENCY_WARN_MS 10000  /* red "release to cancel" screen */
#define EMERGENCY_MS      20000  /* back to the vendor updater */

MYFIFO_INIT(blt_rxfifo, 64, 8);
MYFIFO_INIT(blt_txfifo, 40, 16);

/* Fixed public address A4:C1:38:71:6D:D0 (Telink OUI, little-endian here). Not read from
 * or written to flash: blc_initMacAddress() would write a MAC to 0x76000 if none is found. */
static u8 mac_public[6] = {0xD0, 0x6D, 0x71, 0x38, 0xC1, 0xA4};

static const u8 tbl_advData[] = {
    0x02, 0x01, 0x06,
    0x08, 0x09, 'M', 'o', 'r', 's', 'e', 'O', 'S',
};

extern const u16 dump_data_handle;
#ifdef MOS_HAVE_FITPRO
extern const u16 nus_tx_handle;
#endif
#ifdef MOS_HAVE_BATTERY
extern u8 my_batVal[1];
#endif
extern void my_att_init(void);

static volatile u8  stream_on;
static volatile u32 stream_addr;
static volatile u32 stream_end;
static volatile u8  info_pending;
static volatile u8  reboot_pending;
static volatile u8  updater_pending;
static volatile u8  erase_pending;
static volatile u32 erase_addr;
static volatile u8  commit_pending;
static volatile u32 page_addr = 0xFFFFFFFF;
static u8 page_buf[256];
static volatile u8  status_pending;
static volatile u8  redraw_pending;
static volatile u8  reinit_pending;
static u8 status_pkt[20];
static int connected;

_attribute_ram_code_ void irq_handler(void) { irq_blt_sdk_handler(); }

static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

static void wr32(u8 *p, u32 v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

int dump_onWrite(rf_packet_att_write_t *p) {
    int len = p->l2capLen - 3;
    u8 *d = &p->value;
    if (len < 1) return 0;
    switch (d[0]) {
    case 0x01:
        if (len >= 9) {
            u32 a = rd32(d + 1), n = rd32(d + 5);
            if (a > FLASH_SIZE) a = FLASH_SIZE;
            if (n > FLASH_SIZE - a) n = FLASH_SIZE - a;
            stream_addr = a & ~15u;
            stream_end = a + n;
            stream_on = n > 0;
        }
        break;
    case 0x02:
        stream_on = 0;
        break;
    case 0x03:
        info_pending = 1;
        break;
    case 0x04:
        reboot_pending = 1;
        break;
    case 0x10:
        if (len >= 5 && !erase_pending && !commit_pending) {
            erase_addr = rd32(d + 1);
            erase_pending = 1;
        }
        break;
    case 0x11:
        if (len >= 5) {
            u32 a = rd32(d + 1);
            int i;
            page_addr = (a >= ASSET_START && a < ASSET_END && !(a & 0xff)) ? a : 0xFFFFFFFF;
            for (i = 0; i < 256; i++) page_buf[i] = 0xff;
        }
        break;
    case 0x12:
        if (len >= 3) {
            int off = d[1], n = len - 2, i;
            for (i = 0; i < n && off + i < 256; i++) page_buf[off + i] = d[2 + i];
        }
        break;
    case 0x13:
        if (!erase_pending) commit_pending = 1;
        break;
    case 0x20:
        if (len >= 7 && d[3] && d[4]) {
            lcd.colstart = d[1];
            lcd.rowstart = d[2];
            lcd.width = d[3];
            lcd.height = d[4];
            lcd.madctl = d[5];
            lcd.invert = d[6] ? 1 : 0;
            redraw_pending = 1;
        }
        break;
    case 0x21:
        reinit_pending = 1;
        break;
    case 0x05:
        if (len >= 5 && d[1] == 'U' && d[2] == 'P' && d[3] == 'D' && d[4] == 'T') updater_pending = 1;
        break;
    }
    return 0;
}

static void send_info(void) {
    u8 pkt[20] = {0};
    u8 h0[0x20], h1[0x20];
    flash_read_page(0x00000, sizeof(h0), h0);
    flash_read_page(0x20000, sizeof(h1), h1);
    wr32(pkt, 0xFFFFFFFF);
    pkt[4] = 'L';
    pkt[5] = 'T';
    pkt[6] = 'D';
    pkt[7] = 'P';
    pkt[8] = DUMPER_VERSION;
    pkt[9] = REG_ADDR8(0x63e);       /* boot bank register, 0 = booted from 0x0 */
    wr32(pkt + 10, flash_read_mid());
    pkt[14] = h0[8];                  /* boot flag of bank 0 (0x4b = valid) */
    pkt[15] = h1[8];                  /* boot flag of bank 1 */
    wr32(pkt + 16, rd32(h0 + 0x18));  /* size of the image in bank 0 */
    if (bls_att_pushNotifyData(dump_data_handle, pkt, sizeof(pkt)) == BLE_SUCCESS) info_pending = 0;
}

static void pump_stream(void) {
    u8 pkt[20];
    int burst;
    for (burst = 0; burst < 8 && stream_on; burst++) {
        u32 a = stream_addr;
        wr32(pkt, a);
        flash_read_page(a, 16, pkt + 4);
        if (bls_att_pushNotifyData(dump_data_handle, pkt, sizeof(pkt)) != BLE_SUCCESS) return;  // TX FIFO full
        stream_addr = a + 16;
        if (stream_addr >= stream_end) stream_on = 0;
    }
}

static void set_status(u8 op, u8 st, u32 addr) {
    wr32(status_pkt, 0xFFFFFFFE);
    status_pkt[4] = 'S';
    status_pkt[5] = op;
    status_pkt[6] = st;
    wr32(status_pkt + 7, addr);
    status_pending = 1;
}

static void do_erase(void) {
    u32 a = erase_addr;
    erase_pending = 0;
    if (a < ASSET_START || a >= ASSET_END || (a & 0xfff)) {
        set_status(0x10, 1, a);
        return;
    }
    flash_erase_sector(a);
    set_status(0x10, 0, a);
}

static void do_commit(void) {
    u8 chk[16];
    u32 a = page_addr;
    int i, j;
    commit_pending = 0;
    if (a < ASSET_START || a >= ASSET_END || (a & 0xff)) {
        set_status(0x13, 1, a);
        return;
    }
    flash_write_page(a, 256, page_buf);
    for (i = 0; i < 256; i += 16) {
        flash_read_page(a + i, 16, chk);
        for (j = 0; j < 16; j++)
            if (chk[j] != page_buf[i + j]) {
                set_status(0x13, 2, a + i + j);
                return;
            }
    }
    set_status(0x13, 0, a);
}

/* Go back to the vendor updater in bank 0x20000, as the stock firmware does. */
static void enter_updater(void) {
    u8 h1[12], zero[4] = {0, 0, 0, 0};
    updater_pending = 0;
    if (REG_ADDR8(0x63e) != 0) return;  /* not running from bank 0x0: refuse */
    flash_read_page(0x20000, sizeof(h1), h1);
    if (h1[8] != 0x4b || h1[9] != 'N' || h1[10] != 'L' || h1[11] != 'T') return;  /* no updater */
    flash_write_page(0x8, 4, zero);
    sleep_us(100 * 1000);
    start_reboot();
}

static void notify(const u8 *pkt, int len) {
    if (connected) bls_att_pushNotifyData(dump_data_handle, (u8 *)pkt, len);
}

#ifdef MOS_HAVE_FITPRO
static void nus_tx(const u8 *pkt, int len) {
    if (connected) bls_att_pushNotifyData(nus_tx_handle, (u8 *)pkt, len);
}

static void find_band(void) {
    int i;
    mos_wake();
    for (i = 0; i < 3; i++) {
        mos_buzz(200);
        sleep_us(150 * 1000);
    }
}
#endif

static void buzz(int ms) {
    gpio_write(PIN_MOTOR, 1);
    sleep_us(ms * 1000);
    gpio_write(PIN_MOTOR, 0);

    gpio_set_func(PIN_TOUCH, AS_GPIO);
    gpio_set_output_en(PIN_TOUCH, 0);
    gpio_set_input_en(PIN_TOUCH, 1);
}

static void on_connect(u8 e, u8 *p, int n) {
    connected = 1;
    mos_set_connected(1);
    bls_l2cap_requestConnParamUpdate(6, 12, 0, 400);  // 7.5-15 ms, no latency, 4 s timeout
}

static void on_terminate(u8 e, u8 *p, int n) {
    connected = 0;
    mos_set_connected(0);
    stream_on = 0;
}

int main() {
    /* exactly what the stock firmware does before cpu_wakeup_init(): OTA image up to 124 KB,
     * second bank at 0x20000. Booted from 0x20000, the SDK therefore targets 0x0 for OTA. */
    bls_ota_setFirmwareSizeAndOffset(0x7c, 0x20000);

    blc_pm_select_internal_32k_crystal();
    cpu_wakeup_init();
    clock_init(SYS_CLK_16M_Crystal);
    gpio_init();

    gpio_set_func(PIN_MOTOR, AS_GPIO);
    gpio_set_output_en(PIN_MOTOR, 1);
    gpio_set_input_en(PIN_MOTOR, 0);
    gpio_write(PIN_MOTOR, 0);

    /* crystal cap trim, read-only, as the stock firmware does from 0x7f000 */
    u8 cap;
    flash_read_page(0x7f000, 1, &cap);
    if (cap != 0xff) analog_write(0x81, (analog_read(0x81) & 0xe0) | (cap & 0x1f));

    rf_drv_init(RF_MODE_BLE_1M);

    blc_ll_initBasicMCU(mac_public);
    blc_ll_initAdvertising_module(mac_public);
    blc_ll_initSlaveRole_module();
    blc_ll_initPowerManagement_module();

    my_att_init();
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    bls_smp_enableParing(0);

    bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData));
    if (bls_ll_setAdvParam(ADV_INTERVAL_30MS, ADV_INTERVAL_35MS, ADV_TYPE_CONNECTABLE_UNDIRECTED,
                           OWN_ADDRESS_PUBLIC, 0, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE) != BLE_SUCCESS) {
        while (1) {  // should never happen: buzz forever so it is noticed
            buzz(100);
            sleep_us(900 * 1000);
        }
    }
    bls_ll_setAdvEnable(1);
    rf_set_power_level_index(RF_POWER_7P9dBm);

    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &on_connect);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &on_terminate);
    bls_pm_setSuspendMask(SUSPEND_DISABLE);

    buzz(300);  // "alive"
    lcd_init();
    lcd_backlight(1);
#ifdef MOS_HAVE_FITPRO
    {
        static fitpro_cb_t fp_cb;
        fp_cb.tx = nus_tx;
        fp_cb.find = find_band;
#ifdef MOS_HAVE_MESSAGE
        fp_cb.message = message_show;
#endif
        fitpro_init(&fp_cb);
    }
#endif
#ifdef MOS_HAVE_BATTERY
    battery_update();
#endif
    mos_init(buzz, notify);

    irq_enable();

    int emergency_stage = 0;  /* 0 idle, 1 warning shown, 2 done/refused */
    while (1) {
        blt_sdk_main_loop();

        int t = gpio_read(PIN_TOUCH) ? 1 : 0;
#ifdef MOS_HAVE_RTC
        rtc_poll();
#endif
#ifdef MOS_HAVE_BATTERY
        battery_poll();
        my_batVal[0] = battery_level();
#endif
        mos_poll(t);
#ifdef MOS_HAVE_FITPRO
        fitpro_poll();
#endif

        /* emergency exit, with or without BLE: hold 10 s -> warning, 20 s -> updater */
        if (!t) {
            if (emergency_stage == 1) mos_emergency_warning(0);  /* released: cancelled */
            emergency_stage = 0;
        } else if (emergency_stage == 0 && mos_press_ms() > EMERGENCY_WARN_MS) {
            emergency_stage = 1;
            buzz(400);
            mos_emergency_warning(1);
        } else if (emergency_stage == 1 && mos_press_ms() > EMERGENCY_MS) {
            emergency_stage = 2;  /* if refused, release and start again */
            buzz(100);
            sleep_us(100 * 1000);
            buzz(100);
            sleep_us(100 * 1000);
            buzz(100);
            enter_updater();
        }
        if (reinit_pending) {
            reinit_pending = 0;
            lcd_init();
            lcd_backlight(1);
            redraw_pending = 1;
        }
        if (redraw_pending) {
            redraw_pending = 0;
            lcd_apply();
            mos_redraw();
        }

        if (!connected) continue;
        if (erase_pending) do_erase();
        if (commit_pending) do_commit();
        if (status_pending) {
            if (bls_att_pushNotifyData(dump_data_handle, status_pkt, sizeof(status_pkt)) == BLE_SUCCESS)
                status_pending = 0;
        } else if (info_pending) send_info();
        else if (stream_on) pump_stream();
        if (updater_pending) enter_updater();
        if (reboot_pending) {
            sleep_us(100 * 1000);
            start_reboot();
        }
    }
    return 0;
}
