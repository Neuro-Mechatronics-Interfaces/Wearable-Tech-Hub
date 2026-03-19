#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── HID usage pages ───────────────────────────────────────────────────────────
#define HID_PAGE_GENERIC_DESKTOP  0x01u
#define HID_PAGE_BUTTON           0x09u
#define HID_PAGE_CONSUMER         0x0Cu

// ── Generic Desktop top-level collection usages ───────────────────────────────
#define HID_USAGE_POINTER   0x01u  // Pointer (Physical collection)
#define HID_USAGE_MOUSE     0x02u  // Mouse (Application collection)
#define HID_USAGE_JOYSTICK  0x04u  // Joystick
#define HID_USAGE_GAMEPAD   0x05u  // Game Pad

// ── Generic Desktop axis usages (page 0x01) ───────────────────────────────────
#define HID_USAGE_X         0x30u
#define HID_USAGE_Y         0x31u
#define HID_USAGE_Z         0x32u
#define HID_USAGE_RX        0x33u
#define HID_USAGE_RY        0x34u
#define HID_USAGE_RZ        0x35u
#define HID_USAGE_SLIDER    0x36u
#define HID_USAGE_DIAL      0x37u
#define HID_USAGE_WHEEL     0x38u
#define HID_USAGE_HAT       0x39u

// ── Input item flags ──────────────────────────────────────────────────────────
#define HID_INPUT_DATA      (0u << 0)
#define HID_INPUT_CONSTANT  (1u << 0)
#define HID_INPUT_VARIABLE  (1u << 1)
#define HID_INPUT_RELATIVE  (1u << 2)
#define HID_INPUT_NULL_STATE (1u << 6)

// ── Field descriptor ─────────────────────────────────────────────────────────
// One entry per "Input (Data, Variable, …)" item emitted by the parser.
// For arrays we expand one field per usage in [usage_min, usage_max].
typedef struct {
    uint32_t usage_page;
    uint32_t usage;         // Specific usage or usage_min for range
    uint32_t usage_max;     // == usage for single-usage fields
    int32_t  logical_min;
    int32_t  logical_max;
    uint8_t  report_id;     // 0 = report has no ID prefix byte
    uint32_t bit_offset;    // Bit offset within the report body (after ID byte)
    uint8_t  bit_size;      // Width of this field in bits
    uint8_t  flags;         // HID_INPUT_* flags
} hid_field_t;

#define HID_MAX_FIELDS 64

typedef struct {
    hid_field_t fields[HID_MAX_FIELDS];
    int         count;
    uint32_t    collection_usage; // top-level Application Collection usage (HID_USAGE_MOUSE etc.)
} hid_descriptor_t;

// ── API ───────────────────────────────────────────────────────────────────────

// Parse a raw HID Report Map (as read from the GATT Report Map characteristic
// 0x2A4B) into a hid_descriptor_t.
//
// Returns the number of fields parsed (same as desc->count), or -1 on error.
int hid_parse_descriptor(const uint8_t *data, uint16_t len,
                          hid_descriptor_t *desc);

// Extract a signed integer value from a packed HID report byte array.
// bit_offset is relative to the start of the report body (i.e. after the
// optional report-ID prefix byte, which the caller has already consumed).
int32_t hid_extract_field(const uint8_t *report, uint16_t report_len,
                           uint32_t bit_offset, uint8_t bit_size,
                           bool is_signed);
