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

    DH_ATT_END_H,
} DH_ATT_HANDLE;

const u16 dump_data_handle = DH_DUMP_DATA_DP_H;

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

static const u8 my_devName[] = {'L', 'T', '7', '1', '6', '-', 'D', 'U', 'M', 'P'};

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
};

void my_att_init(void) { bls_att_setAttributeTable((u8 *)my_Attributes); }
