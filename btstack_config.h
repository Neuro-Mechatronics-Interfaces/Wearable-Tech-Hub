#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ── Time / assert ─────────────────────────────────────────────────────────────
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT

// ── LE roles ──────────────────────────────────────────────────────────────────
// Central only — we scan and connect; we do NOT advertise ourselves as a
// peripheral (no BLE HID server role needed).
#define ENABLE_LE_CENTRAL

// ── Security / bonding ────────────────────────────────────────────────────────
// Just Works pairing so headless operation works with any controller.
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#define ENABLE_SOFTWARE_AES128

// ── Logging ───────────────────────────────────────────────────────────────────
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO
#define ENABLE_PRINTF_HEXDUMP

// ── Buffer sizing ─────────────────────────────────────────────────────────────
#define HCI_OUTGOING_PRE_BUFFER_SIZE    4
#define HCI_INCOMING_PRE_BUFFER_SIZE    6
// Large enough for controller-compressed ATT MTU exchanges
#define HCI_ACL_PAYLOAD_SIZE            (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT    4

// ── Connection / channel counts ───────────────────────────────────────────────
// Support up to 4 simultaneous BLE peripherals.
#define MAX_NR_HCI_CONNECTIONS          4
#define MAX_NR_L2CAP_CHANNELS           10
#define MAX_NR_GATT_CLIENTS             4   // one HIDS client per connection
#define MAX_NR_SM_LOOKUP_ENTRIES        5
#define MAX_NR_WHITELIST_ENTRIES        4
#define MAX_NR_CONTROLLER_ACL_BUFFERS   4

// ── Host flow control ─────────────────────────────────────────────────────────
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN         1024
#define HCI_HOST_ACL_PACKET_NUM         4
#define HCI_HOST_SCO_PACKET_LEN         120
#define HCI_HOST_SCO_PACKET_NUM         3

// ── Persistent storage (BTstack TLV → Pico flash) ────────────────────────────
#define MAX_NR_LE_DEVICE_DB_ENTRIES     8
#define NVM_NUM_DEVICE_DB_ENTRIES       8
#define NVM_NUM_LINK_KEYS               8

// ── GATT / ATT ────────────────────────────────────────────────────────────────
// We only act as GATT client (reading remote HID descriptors, subscribing to
// input-report notifications).  No local ATT server needed.
#define MAX_ATT_DB_SIZE                 256
#define HCI_RESET_RESEND_TIMEOUT_MS     1000

// ── HIDS client descriptor storage ───────────────────────────────────────────
// Each device's Report Map characteristic can be up to 512 bytes; allocate
// storage for all MAX_NR_HCI_CONNECTIONS devices.
#define MAX_NR_HID_SUBEVENT_CONNECTIONS 4

#endif // BTSTACK_CONFIG_H
