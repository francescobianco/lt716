/*
 * GATT table of the LT716 flash dumper.
 *
 *   GAP / GATT       standard
 *   Telink OTA       00010203-0405-0607-0809-0a0b0c0d1912 / ...2b12, the SDK's own otaWrite /
 *                    otaRead: from here the original firmware can always be sent back
 *   Dump service     4c543731-3644-554d-5000-000000000000 ("LT716DUMP")
 *     CMD   ...0001  write: commands, see main.c
 *     DATA  ...0002  notify: <addr u32 LE> <16 bytes>
 *
 * Based on examples/ble_adv of FitPro-LT715-TLSR8232 and the SDK's 5316_ble_sample.
 */

#include <stack/ble/ble.h>
#include "tl_common.h"
#include "bundle.h"

typedef enum {
    DH_ATT_H_START = 0,

    DH_GenericAccess_PS_H,
    DH_GenericAccess_DeviceName_CD_H,
    DH_GenericAccess_DeviceName_DP_H,
    DH_GenericAccess_Appearance_CD_H,
    DH_GenericAccess_Appearance_DP_H,
    DH_CONN_PARAM_CD_H,
    DH_CONN_PARAM_DP_H,

    DH_GenericAttribute_PS_H,
    DH_GenericAttribute_ServiceChanged_CD_H,
    DH_GenericAttribute_ServiceChanged_DP_H,
    DH_GenericAttribute_ServiceChanged_CCB_H,

    DH_OTA_PS_H,
    DH_OTA_CMD_OUT_CD_H,
    DH_OTA_CMD_OUT_DP_H,
    DH_OTA_CMD_OUT_DESC_H,

    DH_DUMP_PS_H,
    DH_DUMP_CMD_CD_H,
    DH_DUMP_CMD_DP_H,
    DH_DUMP_DATA_CD_H,
    DH_DUMP_DATA_DP_H,
    DH_DUMP_DATA_CCB_H,

#ifdef MOS_HAVE_FITPRO
    DH_NUS_PS_H,
    DH_NUS_RX_CD_H,
    DH_NUS_RX_DP_H,
    DH_NUS_TX_CD_H,
    DH_NUS_TX_DP_H,
    DH_NUS_TX_CCB_H,
#endif

#ifdef MOS_HAVE_BATTERY
    DH_BATT_PS_H,
    DH_BATT_LEVEL_CD_H,
    DH_BATT_LEVEL_DP_H,
    DH_BATT_LEVEL_CCB_H,
#endif

    DH_DEVINFO_PS_H,
    DH_DEVINFO_PNP_CD_H,
    DH_DEVINFO_PNP_DP_H,


    DH_ATT_END_H,
} DH_ATT_HANDLE;

const u16 dump_data_handle = DH_DUMP_DATA_DP_H;
#ifdef MOS_HAVE_FITPRO
const u16 nus_tx_handle = DH_NUS_TX_DP_H;
#endif
#ifdef MOS_HAVE_BATTERY
const u16 batt_level_handle = DH_BATT_LEVEL_DP_H;
#endif

typedef struct {
    u16 intervalMin;
    u16 intervalMax;
    u16 latency;
    u16 timeout;
} gap_periConnectParams_t;

static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 my_primaryServiceUUID  = GATT_UUID_PRIMARY_SERVICE;
static const u16 my_characterUUID       = GATT_UUID_CHARACTER;
static const u16 my_devNameUUID         = GATT_UUID_DEVICE_NAME;
static const u16 my_gapServiceUUID      = SERVICE_UUID_GENERIC_ACCESS;
static const u16 my_appearanceUIID      = 0x2a01;
static const u16 my_periConnParamUUID   = 0x2a04;
static const u16 my_gattServiceUUID     = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 serviceChangeUIID      = GATT_UUID_SERVICE_CHANGE;
static const u16 userdesc_UUID          = GATT_UUID_CHAR_USER_DESC;

static u16 my_appearance = GAP_APPEARE_UNKNOWN;
static gap_periConnectParams_t my_periConnParameters = {6, 12, 0, 400};
static u16 serviceChangeVal[2] = {0};
static u8 serviceChangeCCC[2] = {0, 0};

static const u8 my_devName[] = {'M', 'o', 'r', 's', 'e', 'O', 'S'};

/* Telink OTA, exactly as in the SDK sample */
static const u8 my_OtaUUID[16]        = TELINK_SPP_DATA_OTA;
static const u8 my_OtaServiceUUID[16] = TELINK_OTA_UUID_SERVICE;
static u8 my_OtaData = 0x00;
static const u8 my_OtaName[] = {'O', 'T', 'A'};

/* Dump service, UUIDs little-endian: 4c543731-3644-554d-5000-00000000000x */
#define DUMP_UUID(x) {x, 0, 0, 0, 0, 0, 0, 0x50, 0x4d, 0x55, 0x44, 0x36, 0x31, 0x37, 0x54, 0x4c}
static const u8 my_DumpServiceUUID[16] = DUMP_UUID(0x00);
static const u8 my_DumpCmdUUID[16]     = DUMP_UUID(0x01);
static const u8 my_DumpDataUUID[16]    = DUMP_UUID(0x02);
static u8 my_DumpCmd[20];
static u8 my_DumpData[20];
static u8 my_DumpDataCCC[2];

extern int dump_onWrite(rf_packet_att_write_t *p);
#ifdef MOS_HAVE_FITPRO
extern int fitpro_on_write(rf_packet_att_write_t *p);

/* FitPro UART (Nordic UART UUIDs), little-endian (first byte 0x9d!): 6e40000x-b5a3-f393-e0a9-e50e24dcca9d */
#define NUS_UUID(x) {0x9d, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, x, 0x00, 0x40, 0x6e}
static const u8 my_NusServiceUUID[16] = NUS_UUID(0x01);
static const u8 my_NusRxUUID[16]      = NUS_UUID(0x02);
static const u8 my_NusTxUUID[16]      = NUS_UUID(0x03);
static u8 my_NusRx[20];
static u8 my_NusTx[20];
static u8 my_NusTxCCC[2];
static const u8 my_NusRxCharVal[19] = {
    CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP, U16_LO(DH_NUS_RX_DP_H), U16_HI(DH_NUS_RX_DP_H),
    0x9d, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e};
static const u8 my_NusTxCharVal[19] = {
    CHAR_PROP_NOTIFY, U16_LO(DH_NUS_TX_DP_H), U16_HI(DH_NUS_TX_DP_H),
    0x9d, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e};

#endif

/* Device information: PnP ID (HOGP hosts read it) */
static const u16 my_devServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;
static const u16 my_PnPUUID = CHARACTERISTIC_UUID_PNP_ID;
static const u8 my_PnP[7] = {0x02, 0x8a, 0x24, 0x66, 0x82, 0x04, 0x00};
static const u8 my_PnPCharVal[5] = {
    CHAR_PROP_READ, U16_LO(DH_DEVINFO_PNP_DP_H), U16_HI(DH_DEVINFO_PNP_DP_H),
    U16_LO(CHARACTERISTIC_UUID_PNP_ID), U16_HI(CHARACTERISTIC_UUID_PNP_ID)};

#ifdef MOS_HAVE_BATTERY
/* Battery service */
static const u16 my_batServiceUUID = SERVICE_UUID_BATTERY;
static const u16 my_batCharUUID = CHARACTERISTIC_UUID_BATTERY_LEVEL;
u8 my_batVal[1] = {100};
static u8 my_batCCC[2];
static const u8 my_batCharVal[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY, U16_LO(DH_BATT_LEVEL_DP_H), U16_HI(DH_BATT_LEVEL_DP_H),
    U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL)};

static const u8 my_devNameCharVal[5] = {
    CHAR_PROP_READ, U16_LO(DH_GenericAccess_DeviceName_DP_H), U16_HI(DH_GenericAccess_DeviceName_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)};
static const u8 my_appearanceCharVal[5] = {
    CHAR_PROP_READ, U16_LO(DH_GenericAccess_Appearance_DP_H), U16_HI(DH_GenericAccess_Appearance_DP_H),
    U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)};
static const u8 my_periConnParamCharVal[5] = {
    CHAR_PROP_READ, U16_LO(DH_CONN_PARAM_DP_H), U16_HI(DH_CONN_PARAM_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)};
static const u8 my_serviceChangeCharVal[5] = {
    CHAR_PROP_INDICATE, U16_LO(DH_GenericAttribute_ServiceChanged_DP_H),
    U16_HI(DH_GenericAttribute_ServiceChanged_DP_H),
    U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)};
static const u8 my_OtaCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
    U16_LO(DH_OTA_CMD_OUT_DP_H), U16_HI(DH_OTA_CMD_OUT_DP_H),
    /* TELINK_SPP_DATA_OTA, spelled out: the macro is a braced list */
    0x12, 0x2B, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
static const u8 my_DumpCmdCharVal[19] = {
    CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP,
    U16_LO(DH_DUMP_CMD_DP_H), U16_HI(DH_DUMP_CMD_DP_H),
    0x01, 0, 0, 0, 0, 0, 0, 0x50, 0x4d, 0x55, 0x44, 0x36, 0x31, 0x37, 0x54, 0x4c};
static const u8 my_DumpDataCharVal[19] = {
    CHAR_PROP_NOTIFY,
    U16_LO(DH_DUMP_DATA_DP_H), U16_HI(DH_DUMP_DATA_DP_H),
    0x02, 0, 0, 0, 0, 0, 0, 0x50, 0x4d, 0x55, 0x44, 0x36, 0x31, 0x37, 0x54, 0x4c};

#endif

const attribute_t my_Attributes[] = {
    {DH_ATT_END_H - 1, 0, 0, 0, 0, 0},  // total number of attributes

    // GAP
    {7, ATT_PERMISSIONS_READ, 2, 2, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_gapServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devNameCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_devNameCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devName), (u8 *)(&my_devNameUUID), (u8 *)(my_devName), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearanceCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_appearanceCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearance), (u8 *)(&my_appearanceUIID), (u8 *)(&my_appearance), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParamCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_periConnParamCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParameters), (u8 *)(&my_periConnParamUUID), (u8 *)(&my_periConnParameters), 0},

    // GATT
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_gattServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_serviceChangeCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_serviceChangeCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(serviceChangeVal), (u8 *)(&serviceChangeUIID), (u8 *)(&serviceChangeVal), 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(serviceChangeCCC), (u8 *)(&clientCharacterCfgUUID), (u8 *)(serviceChangeCCC), 0},

    // Telink OTA
    {4, ATT_PERMISSIONS_READ, 2, 16, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_OtaServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_OtaCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_OtaCharVal), 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(my_OtaData), (u8 *)(&my_OtaUUID), (&my_OtaData), &otaWrite, &otaRead},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_OtaName), (u8 *)(&userdesc_UUID), (u8 *)(my_OtaName), 0},

    // Dump service
    {6, ATT_PERMISSIONS_READ, 2, 16, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_DumpServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_DumpCmdCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_DumpCmdCharVal), 0},
    {0, ATT_PERMISSIONS_WRITE, 16, sizeof(my_DumpCmd), (u8 *)(&my_DumpCmdUUID), (u8 *)(my_DumpCmd), (att_readwrite_callback_t)&dump_onWrite},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_DumpDataCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_DumpDataCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(my_DumpData), (u8 *)(&my_DumpDataUUID), (u8 *)(my_DumpData), 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(my_DumpDataCCC), (u8 *)(&clientCharacterCfgUUID), (u8 *)(my_DumpDataCCC), 0},

#ifdef MOS_HAVE_FITPRO
    // FitPro UART
    {6, ATT_PERMISSIONS_READ, 2, 16, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_NusServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_NusRxCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_NusRxCharVal), 0},
    {0, ATT_PERMISSIONS_WRITE, 16, sizeof(my_NusRx), (u8 *)(&my_NusRxUUID), (u8 *)(my_NusRx), (att_readwrite_callback_t)&fitpro_on_write},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_NusTxCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_NusTxCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(my_NusTx), (u8 *)(&my_NusTxUUID), (u8 *)(my_NusTx), 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(my_NusTxCCC), (u8 *)(&clientCharacterCfgUUID), (u8 *)(my_NusTxCCC), 0},

#endif

#ifdef MOS_HAVE_BATTERY
    // Battery
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_batServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_batCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_batCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_batVal), (u8 *)(&my_batCharUUID), (u8 *)(my_batVal), 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(my_batCCC), (u8 *)(&clientCharacterCfgUUID), (u8 *)(my_batCCC), 0},

#endif

    // Device information
    {3, ATT_PERMISSIONS_READ, 2, 2, (u8 *)(&my_primaryServiceUUID), (u8 *)(&my_devServiceUUID), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_PnPCharVal), (u8 *)(&my_characterUUID), (u8 *)(my_PnPCharVal), 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_PnP), (u8 *)(&my_PnPUUID), (u8 *)(my_PnP), 0},

};

void my_att_init(void) { bls_att_setAttributeTable((u8 *)my_Attributes); }
