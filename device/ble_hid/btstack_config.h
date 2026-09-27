/*
 * btstack_config.h — BTstack compile-time configuration for μT-Kernel
 *
 * Minimum configuration for BLE HID Keyboard on Pico W (CYW43439)
 * LE Peripheral (HOGP) + Central (split slave 接続)
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

/* BLE 有効化 (必須) */
#define ENABLE_BLE

/* LE Peripheral (HID to host) + Central (split slave connection) */
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL

/* ATT/GATT server (HID Service) + client (split Matrix Service) */
#define ENABLE_ATT_SERVER
#define ENABLE_GATT_CLIENT

/* Log level */
#define ENABLE_LOG_ERROR

/* Timer support */
#define HAVE_EMBEDDED_TIME_MS

/* HCI configuration */
#define HCI_ACL_PAYLOAD_SIZE            251
#define HCI_OUTGOING_PRE_BUFFER_SIZE    4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT    4
#define HCI_RESET_RESEND_TIMEOUT_MS     1000

/* Security Manager — LE Secure Connections (iOS/Android HID 必須) */
#define ENABLE_LE_SECURE_CONNECTIONS

/* BLE プライバシー */
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION

/* ATT server CCC persistence */
#define NVM_NUM_GATT_SERVER_CCC         8

/* 静的プールサイズ (btstack_memory.c が使用) */
#define MAX_NR_HCI_CONNECTIONS          4
#define MAX_NR_L2CAP_CHANNELS           4
#define MAX_NR_L2CAP_SERVICES           2
#define MAX_NR_GATT_CLIENTS             1
#define MAX_NR_ATT_DB_CACHE_ENTRIES     1
#define MAX_NR_SM_LOOKUP_ENTRIES        3
#define MAX_NR_WHITELIST_ENTRIES        4
#define MAX_NR_LE_DEVICE_DB_ENTRIES     4
#define NVM_NUM_DEVICE_DB_ENTRIES       4

#endif /* BTSTACK_CONFIG_H */
