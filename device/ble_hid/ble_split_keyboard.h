
// clang-format off
// device/ble_hid/ble_split_keyboard.h generated from device/ble_hid/ble_split_keyboard.gatt for BTstack
// it needs to be regenerated when the .gatt file is updated. 

// To generate device/ble_hid/ble_split_keyboard.h:
// lib/btstack/tool/compile_gatt.py device/ble_hid/ble_split_keyboard.gatt device/ble_hid/ble_split_keyboard.h

// att db format version 1

// binary attribute representation:
// - size in bytes (16), flags(16), handle (16), uuid (16/128), value(...)

#include <stdint.h>

// Reference: https://en.cppreference.com/w/cpp/feature_test
#if __cplusplus >= 200704L
constexpr
#endif
static const uint8_t profile_data[] =
{
    // ATT DB Version
    1,

    // 0x0001 PRIMARY_SERVICE-GAP_SERVICE
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18, 
    // 0x0002 CHARACTERISTIC-GAP_DEVICE_NAME - READ
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a, 
    // 0x0003 VALUE CHARACTERISTIC-GAP_DEVICE_NAME - READ -'TK Split'
    // READ_ANYBODY
    0x10, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x2a, 0x54, 0x4b, 0x20, 0x53, 0x70, 0x6c, 0x69, 0x74, 
    // 0x0004 CHARACTERISTIC-GAP_APPEARANCE - READ
    0x0d, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x28, 0x02, 0x05, 0x00, 0x01, 0x2a, 
    // 0x0005 VALUE CHARACTERISTIC-GAP_APPEARANCE - READ -'C1 03'
    // READ_ANYBODY
    0x0a, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x2a, 0xC1, 0x03, 
    // Custom Matrix Service for split keyboard
    // Service UUID: 4b4b5348-5f53-504c-4954-000000000001
    // 0x0006 PRIMARY_SERVICE-4b4b5348-5f53-504c-4954-000000000001
    0x18, 0x00, 0x02, 0x00, 0x06, 0x00, 0x00, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x49, 0x4c, 0x50, 0x53, 0x5f, 0x48, 0x53, 0x4b, 0x4b, 
    // Matrix State Characteristic (Notify + Read)
    // UUID: 4b4b5348-5f53-504c-4954-000000000002
    // 0x0007 CHARACTERISTIC-4b4b5348-5f53-504c-4954-000000000002 - READ | NOTIFY | DYNAMIC
    0x1b, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x12, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x49, 0x4c, 0x50, 0x53, 0x5f, 0x48, 0x53, 0x4b, 0x4b, 
    // 0x0008 VALUE CHARACTERISTIC-4b4b5348-5f53-504c-4954-000000000002 - READ | NOTIFY | DYNAMIC
    // READ_ANYBODY
    0x16, 0x00, 0x02, 0x03, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x49, 0x4c, 0x50, 0x53, 0x5f, 0x48, 0x53, 0x4b, 0x4b, 
    // 0x0009 CLIENT_CHARACTERISTIC_CONFIGURATION
    // READ_ANYBODY, WRITE_ANYBODY
    0x0a, 0x00, 0x0e, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00, 
    // END
    0x00, 0x00, 
}; // total size 89 bytes 


//
// list service handle ranges
//
#define ATT_SERVICE_GAP_SERVICE_START_HANDLE 0x0001
#define ATT_SERVICE_GAP_SERVICE_END_HANDLE 0x0005
#define ATT_SERVICE_GAP_SERVICE_01_START_HANDLE 0x0001
#define ATT_SERVICE_GAP_SERVICE_01_END_HANDLE 0x0005
#define ATT_SERVICE_4b4b5348_5f53_504c_4954_000000000001_START_HANDLE 0x0006
#define ATT_SERVICE_4b4b5348_5f53_504c_4954_000000000001_END_HANDLE 0x0009
#define ATT_SERVICE_4b4b5348_5f53_504c_4954_000000000001_01_START_HANDLE 0x0006
#define ATT_SERVICE_4b4b5348_5f53_504c_4954_000000000001_01_END_HANDLE 0x0009

//
// list mapping between characteristics and handles
//
#define ATT_CHARACTERISTIC_GAP_DEVICE_NAME_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_GAP_APPEARANCE_01_VALUE_HANDLE 0x0005
#define ATT_CHARACTERISTIC_4b4b5348_5f53_504c_4954_000000000002_01_VALUE_HANDLE 0x0008
#define ATT_CHARACTERISTIC_4b4b5348_5f53_504c_4954_000000000002_01_CLIENT_CONFIGURATION_HANDLE 0x0009
