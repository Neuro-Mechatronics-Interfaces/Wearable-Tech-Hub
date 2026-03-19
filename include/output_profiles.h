#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hid_merger.h"

// ── Output profiles ───────────────────────────────────────────────────────────
// Three profiles, each matching the USB HID report layout of a real console
// controller.  Exactly one is active at a time; switching requires a brief
// USB re-enumeration so the host sees the updated descriptor.

typedef enum {
    PROFILE_SWITCH   = 0,  // Nintendo Switch Pro Controller style
    PROFILE_PS5      = 1,  // Sony DualSense style
    PROFILE_XBOX     = 2,  // Xbox One / Series style
} output_profile_t;

// ── Report structures ─────────────────────────────────────────────────────────
//
// Button bit assignments (bit 0 = button 1 = lowest bit of buttons field):
//
//   PROFILE_SWITCH (7 bytes)
//     Bit  0: B (south)       Bit  1: A (east)
//     Bit  2: Y (west)        Bit  3: X (north)
//     Bit  4: L               Bit  5: R
//     Bit  6: ZL              Bit  7: ZR
//     Bit  8: Minus           Bit  9: Plus
//     Bit 10: L3              Bit 11: R3
//     Bit 12: Home            Bit 13: Capture
//     [bits 14-15 padding]
//
//   PROFILE_PS5 (9 bytes)
//     Bit  0: Cross  ×        Bit  1: Circle  ○
//     Bit  2: Square □        Bit  3: Triangle △
//     Bit  4: L1              Bit  5: R1
//     Bit  6: L2 (digital)    Bit  7: R2 (digital)
//     Bit  8: Create          Bit  9: Options
//     Bit 10: L3              Bit 11: R3
//     Bit 12: PS              Bit 13: Touchpad click
//     [bits 14-15 padding]
//
//   PROFILE_XBOX (13 bytes)
//     Bit  0: A (south)       Bit  1: B (east)
//     Bit  2: X (west)        Bit  3: Y (north)
//     Bit  4: LB              Bit  5: RB
//     Bit  6: LT (digital)    Bit  7: RT (digital)
//     Bit  8: View            Bit  9: Menu
//     Bit 10: L3              Bit 11: R3
//     Bit 12: Guide           Bit 13: Share
//     [bits 14-15 padding]

// PROFILE_SWITCH — 7 bytes, no report ID
// Sticks uint8 [0,255], 128 = center.  ZL/ZR are digital buttons only.
typedef struct __attribute__((packed)) {
    uint16_t buttons;   // bits 0-13 as above; bits 14-15 padding
    uint8_t  hat;       // bits [3:0]: 0-7 direction, 0xF = center; bits [7:4] padding
    uint8_t  lx;        // left stick X  (0-255, 128 = center)
    uint8_t  ly;        // left stick Y  (0-255, 128 = center)
    uint8_t  rx;        // right stick X (0-255, 128 = center)
    uint8_t  ry;        // right stick Y (0-255, 128 = center)
} switch_report_t;

// PROFILE_PS5 — 9 bytes, no report ID
// Sticks and analog triggers uint8 [0,255], 128 = center for sticks.
typedef struct __attribute__((packed)) {
    uint8_t  lx;        // left stick X  (0-255, 128 = center)
    uint8_t  ly;        // left stick Y  (0-255, 128 = center)
    uint8_t  rx;        // right stick X (0-255, 128 = center)
    uint8_t  ry;        // right stick Y (0-255, 128 = center)
    uint8_t  lt;        // L2 analog     (0-255)
    uint8_t  rt;        // R2 analog     (0-255)
    uint16_t buttons;   // bits 0-13 as above; bits 14-15 padding
    uint8_t  hat;       // bits [3:0]: 0-7 direction, 0xF = center; bits [7:4] padding
} ps5_report_t;

// PROFILE_XBOX — 13 bytes, no report ID
// Sticks int16 [-32768, 32767].  Triggers uint8 [0, 255].
typedef struct __attribute__((packed)) {
    uint16_t buttons;   // bits 0-13 as above; bits 14-15 padding
    uint8_t  lt;        // LT analog (0-255)
    uint8_t  rt;        // RT analog (0-255)
    int16_t  lx;        // left stick X  (-32768..32767)
    int16_t  ly;        // left stick Y  (-32768..32767)
    int16_t  rx;        // right stick X (-32768..32767)
    int16_t  ry;        // right stick Y (-32768..32767)
    uint8_t  hat;       // bits [3:0]: 0-7 direction, 0xF = center; bits [7:4] padding
} xbox_report_t;

// ── HID descriptor blobs ──────────────────────────────────────────────────────
extern const uint8_t  switch_hid_descriptor[];
extern const uint16_t switch_hid_descriptor_len;

extern const uint8_t  ps5_hid_descriptor[];
extern const uint16_t ps5_hid_descriptor_len;

extern const uint8_t  xbox_hid_descriptor[];
extern const uint16_t xbox_hid_descriptor_len;

// ── API ───────────────────────────────────────────────────────────────────────

// Return the active descriptor blob + length.
const uint8_t *profile_descriptor(output_profile_t p, uint16_t *len_out);

// Build a USB HID report from the current merger state.
// buf must be at least profile_report_size(p) bytes.
// Returns the number of bytes written.
uint8_t profile_build_report(output_profile_t p,
                              const merger_state_t *m,
                              uint8_t *buf);

// Return the byte length of a report for the given profile.
uint8_t profile_report_size(output_profile_t p);
