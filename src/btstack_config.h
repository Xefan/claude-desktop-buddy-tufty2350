// BTstack config for the Tufty 2350 Claude Buddy. BLE peripheral only — no
// classic BT, no central role, no bonding yet (security manager is initialized
// so ATT clients can still discover services).
#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_MALLOC

// pico-sdk sets ENABLE_BLE itself when pico_btstack_ble is linked; guard
// to avoid the "redefined" warning when our config is included afterwards.
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

#define HCI_ACL_PAYLOAD_SIZE       200
#define MAX_NR_HCI_CONNECTIONS     1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 1
#define MAX_NR_SM_LOOKUP_ENTRIES   3
#define MAX_NR_WHITELIST_ENTRIES   1

// Persistent device DB (bonds). Even though we don't bond yet,
// le_device_db_tlv.c still gets compiled and insists on a non-zero entry count.
#define NVM_NUM_DEVICE_DB_ENTRIES  4

// CYW43 HCI transport requires a 4-byte prefix slot on outgoing packets
// (for its SDIO/SPI packet header) and 4-byte alignment for ACL chunks.
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#endif
