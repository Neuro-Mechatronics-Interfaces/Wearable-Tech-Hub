#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hid_merger.h"

// ── Output profiles ───────────────────────────────────────────────────────────
// Three output profiles are supported.  Exactly one is active at a time.
// Switching profiles requires a USB re-enumeration (the Pico briefly
// disconnects its D+ pull-up, then reconnects).

typedef enum {
    PROFILE_GAMEPAD  = 0,  // Xbox-compatible 16-button gamepad + 6 axes + hat
    PROFILE_MOUSE    = 1,  // 5-button relative mouse + scroll wheel
    PROFILE_JOYSTICK = 2,  // 32-button + 8 absolute axes + hat (extended)
} output_profile_t;

// ── Report structures ─────────────────────────────────────────────────────────

// PROFILE_GAMEPAD — 9 bytes, no report ID prefix
typedef struct __attribute__((packed)) {
    uint16_t buttons;  // bits 0-15 → button 1-16
    int8_t   lx;       // Left stick X
    int8_t   ly;       // Left stick Y
    int8_t   rx;       // Right stick X  (HID RX / Rx usage)
    int8_t   ry;       // Right stick Y  (HID RY / Ry usage)
    uint8_t  lt;       // Left trigger   (HID Z, unsigned 0-255)
    uint8_t  rt;       // Right trigger  (HID RZ, unsigned 0-255)
    uint8_t  hat;      // low nibble: 0-7 direction, 0xF = centre; high = pad
} gamepad_report_t;

// PROFILE_MOUSE — 4 bytes, no report ID prefix
typedef struct __attribute__((packed)) {
    uint8_t buttons;   // bits 0-4 → buttons 1-5; bits 5-7 = pad
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
} mouse_report_t;

// PROFILE_JOYSTICK — 13 bytes, no report ID prefix
typedef struct __attribute__((packed)) {
    uint32_t buttons;  // bits 0-31 → buttons 1-32
    int8_t   x;
    int8_t   y;
    int8_t   z;
    int8_t   rx;
    int8_t   ry;
    int8_t   rz;
    int8_t   slider;
    int8_t   dial;
    uint8_t  hat;      // nibble, same encoding as gamepad
} joystick_report_t;

// ── HID descriptor blobs ──────────────────────────────────────────────────────
extern const uint8_t gamepad_hid_descriptor[];
extern const uint16_t gamepad_hid_descriptor_len;

extern const uint8_t mouse_hid_descriptor[];
extern const uint16_t mouse_hid_descriptor_len;

extern const uint8_t joystick_hid_descriptor[];
extern const uint16_t joystick_hid_descriptor_len;

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
