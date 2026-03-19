#include "output_profiles.h"
#include <string.h>

// ── Nintendo Switch Pro Controller style ──────────────────────────────────────
// 7 bytes.  14 buttons + 2-bit pad + hat nibble + 4-bit pad + LX LY RX RY uint8.
// Sticks use logical range [0, 255], 128 = center, matching the Pro Controller
// USB report.  ZL/ZR are digital buttons (bits 6-7); no analog trigger axes.
const uint8_t switch_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x05,         // Usage (Game Pad)
    0xA1, 0x01,         // Collection (Application)

    // 14 buttons (B A Y X L R ZL ZR Minus Plus L3 R3 Home Capture)
    0x05, 0x09,         //   Usage Page (Button)
    0x19, 0x01,         //   Usage Minimum (1)
    0x29, 0x0E,         //   Usage Maximum (14)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x0E,         //   Report Count (14)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // 2-bit constant padding → completes the uint16_t buttons field
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x03,         //   Input (Constant)

    // Hat switch (4 bits) + 4-bit constant padding → hat byte
    0x05, 0x01,         //   Usage Page (Generic Desktop)
    0x09, 0x39,         //   Usage (Hat switch)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x07,         //   Logical Maximum (7)
    0x35, 0x00,         //   Physical Minimum (0)
    0x46, 0x3B, 0x01,   //   Physical Maximum (315 degrees)
    0x65, 0x14,         //   Unit (English Rotation: Degrees)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x42,         //   Input (Data, Variable, Absolute, Null state)
    0x65, 0x00,         //   Unit (None) — reset
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x03,         //   Input (Constant)

    // Left stick X/Y: uint8 [0, 255], 128 = center
    0x09, 0x30,         //   Usage (X)
    0x09, 0x31,         //   Usage (Y)
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Right stick X/Y: uint8 [0, 255]
    0x09, 0x33,         //   Usage (Rx)
    0x09, 0x34,         //   Usage (Ry)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    0xC0,               // End Collection
};
const uint16_t switch_hid_descriptor_len = sizeof(switch_hid_descriptor);

// ── Sony DualSense style ──────────────────────────────────────────────────────
// 9 bytes.  LX LY RX RY L2 R2 uint8 + 14 buttons + 2-bit pad + hat nibble +
// 4-bit pad.  Sticks and analog triggers [0, 255], 128 = center for sticks.
const uint8_t ps5_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x05,         // Usage (Game Pad)
    0xA1, 0x01,         // Collection (Application)

    // Sticks + analog triggers: LX LY RX RY L2 R2, each uint8 [0, 255]
    0x09, 0x30,         //   Usage (X)   — LX
    0x09, 0x31,         //   Usage (Y)   — LY
    0x09, 0x33,         //   Usage (Rx)  — RX
    0x09, 0x34,         //   Usage (Ry)  — RY
    0x09, 0x32,         //   Usage (Z)   — L2 analog
    0x09, 0x35,         //   Usage (Rz)  — R2 analog
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x06,         //   Report Count (6)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // 14 buttons (× ○ □ △ L1 R1 L2d R2d Create Options L3 R3 PS Touchpad)
    0x05, 0x09,         //   Usage Page (Button)
    0x19, 0x01,         //   Usage Minimum (1)
    0x29, 0x0E,         //   Usage Maximum (14)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x0E,         //   Report Count (14)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // 2-bit constant padding
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x03,         //   Input (Constant)

    // Hat switch (4 bits) + 4-bit constant padding
    0x05, 0x01,         //   Usage Page (Generic Desktop)
    0x09, 0x39,         //   Usage (Hat switch)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x07,         //   Logical Maximum (7)
    0x35, 0x00,         //   Physical Minimum (0)
    0x46, 0x3B, 0x01,   //   Physical Maximum (315 degrees)
    0x65, 0x14,         //   Unit (English Rotation: Degrees)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x42,         //   Input (Data, Variable, Absolute, Null state)
    0x65, 0x00,         //   Unit (None)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x03,         //   Input (Constant)

    0xC0,               // End Collection
};
const uint16_t ps5_hid_descriptor_len = sizeof(ps5_hid_descriptor);

// ── Xbox One / Series style ───────────────────────────────────────────────────
// 13 bytes.  14 buttons + 2-bit pad + LT RT uint8 + LX LY RX RY int16 + hat.
// Sticks use signed int16 [-32768, 32767] matching the XInput axis range.
// Triggers uint8 [0, 255].
const uint8_t xbox_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x05,         // Usage (Game Pad)
    0xA1, 0x01,         // Collection (Application)

    // 14 buttons (A B X Y LB RB LT_d RT_d View Menu L3 R3 Guide Share)
    0x05, 0x09,         //   Usage Page (Button)
    0x19, 0x01,         //   Usage Minimum (1)
    0x29, 0x0E,         //   Usage Maximum (14)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x0E,         //   Report Count (14)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // 2-bit constant padding
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x03,         //   Input (Constant)

    // LT, RT: uint8 [0, 255]
    0x05, 0x01,         //   Usage Page (Generic Desktop)
    0x09, 0x32,         //   Usage (Z)   — LT
    0x09, 0x35,         //   Usage (Rz)  — RT
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Left stick X/Y: int16 [-32768, 32767]
    0x09, 0x30,         //   Usage (X)
    0x09, 0x31,         //   Usage (Y)
    0x16, 0x00, 0x80,   //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,   //   Logical Maximum (32767)
    0x75, 0x10,         //   Report Size (16)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Right stick X/Y: int16 [-32768, 32767]
    0x09, 0x33,         //   Usage (Rx)
    0x09, 0x34,         //   Usage (Ry)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Hat switch (4 bits) + 4-bit constant padding
    0x09, 0x39,         //   Usage (Hat switch)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x07,         //   Logical Maximum (7)
    0x35, 0x00,         //   Physical Minimum (0)
    0x46, 0x3B, 0x01,   //   Physical Maximum (315 degrees)
    0x65, 0x14,         //   Unit (English Rotation: Degrees)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x42,         //   Input (Data, Variable, Absolute, Null state)
    0x65, 0x00,         //   Unit (None)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x03,         //   Input (Constant)

    0xC0,               // End Collection
};
const uint16_t xbox_hid_descriptor_len = sizeof(xbox_hid_descriptor);

// ── Profile dispatch ──────────────────────────────────────────────────────────

const uint8_t *profile_descriptor(output_profile_t p, uint16_t *len_out) {
    switch (p) {
        case PROFILE_SWITCH:
            *len_out = switch_hid_descriptor_len;
            return switch_hid_descriptor;
        case PROFILE_PS5:
            *len_out = ps5_hid_descriptor_len;
            return ps5_hid_descriptor;
        case PROFILE_XBOX:
            *len_out = xbox_hid_descriptor_len;
            return xbox_hid_descriptor;
    }
    *len_out = 0;
    return NULL;
}

uint8_t profile_report_size(output_profile_t p) {
    switch (p) {
        case PROFILE_SWITCH:  return sizeof(switch_report_t);
        case PROFILE_PS5:     return sizeof(ps5_report_t);
        case PROFILE_XBOX:    return sizeof(xbox_report_t);
    }
    return 0;
}

// Signed int8 [-127, 127] → unsigned uint8 [1, 255], center = 128.
// Used for Switch and PS5 sticks.
static inline uint8_t axis_to_u8(int8_t v) {
    return (uint8_t)((int16_t)v + 128);
}

// Signed int8 [-127, 127] → signed int16 [-32512, 32512].
// Covers ~99% of the Xbox [-32768, 32767] range without overflow.
static inline int16_t axis_to_i16(int8_t v) {
    return (int16_t)v * 256;
}

// Signed int8 [-127, 127] → unsigned uint8 [0, 254] for analog triggers.
static inline uint8_t trigger_to_u8(int8_t v) {
    return (uint8_t)((int16_t)v + 127);
}

uint8_t profile_build_report(output_profile_t p,
                              const merger_state_t *m,
                              uint8_t *buf) {
    switch (p) {

    case PROFILE_SWITCH: {
        switch_report_t r;
        memset(&r, 0, sizeof(r));
        r.buttons = (uint16_t)(merger_get_buttons_word(m) & 0x3FFF);
        uint8_t hat = merger_hat(m);
        r.hat  = (hat <= 7) ? hat : 0x0F;
        r.lx   = axis_to_u8(merger_get_output(m, OUT_LX));
        r.ly   = axis_to_u8(merger_get_output(m, OUT_LY));
        r.rx   = axis_to_u8(merger_get_output(m, OUT_RX));
        r.ry   = axis_to_u8(merger_get_output(m, OUT_RY));
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }

    case PROFILE_PS5: {
        ps5_report_t r;
        memset(&r, 0, sizeof(r));
        r.lx      = axis_to_u8(merger_get_output(m, OUT_LX));
        r.ly      = axis_to_u8(merger_get_output(m, OUT_LY));
        r.rx      = axis_to_u8(merger_get_output(m, OUT_RX));
        r.ry      = axis_to_u8(merger_get_output(m, OUT_RY));
        r.lt      = trigger_to_u8(merger_get_output(m, OUT_LT));
        r.rt      = trigger_to_u8(merger_get_output(m, OUT_RT));
        r.buttons = (uint16_t)(merger_get_buttons_word(m) & 0x3FFF);
        uint8_t hat = merger_hat(m);
        r.hat = (hat <= 7) ? hat : 0x0F;
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }

    case PROFILE_XBOX: {
        xbox_report_t r;
        memset(&r, 0, sizeof(r));
        r.buttons = (uint16_t)(merger_get_buttons_word(m) & 0x3FFF);
        r.lt = trigger_to_u8(merger_get_output(m, OUT_LT));
        r.rt = trigger_to_u8(merger_get_output(m, OUT_RT));
        r.lx = axis_to_i16(merger_get_output(m, OUT_LX));
        r.ly = axis_to_i16(merger_get_output(m, OUT_LY));
        r.rx = axis_to_i16(merger_get_output(m, OUT_RX));
        r.ry = axis_to_i16(merger_get_output(m, OUT_RY));
        uint8_t hat = merger_hat(m);
        r.hat = (hat <= 7) ? hat : 0x0F;
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }

    }
    return 0;
}
