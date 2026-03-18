#include "output_profiles.h"
#include <string.h>

// ── Gamepad HID descriptor ────────────────────────────────────────────────────
// 16 buttons, LX/LY/RX/RY (int8), LT/RT (uint8), hat (nibble + 4-bit pad).
// No report ID — Android and most consoles handle this more reliably.
const uint8_t gamepad_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x05,         // Usage (Game Pad)
    0xA1, 0x01,         // Collection (Application)

    // 16 buttons
    0x05, 0x09,         //   Usage Page (Button)
    0x19, 0x01,         //   Usage Minimum (Button 1)
    0x29, 0x10,         //   Usage Maximum (Button 16)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x10,         //   Report Count (16)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Left stick X/Y
    0x05, 0x01,         //   Usage Page (Generic Desktop)
    0x09, 0x30,         //   Usage (X)
    0x09, 0x31,         //   Usage (Y)
    0x15, 0x81,         //   Logical Minimum (-127)
    0x25, 0x7F,         //   Logical Maximum (127)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Right stick X/Y
    0x09, 0x33,         //   Usage (Rx)
    0x09, 0x34,         //   Usage (Ry)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Triggers (unsigned 0-255)
    0x09, 0x32,         //   Usage (Z)   — left trigger
    0x09, 0x35,         //   Usage (Rz)  — right trigger
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x95, 0x02,         //   Report Count (2)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Hat switch (4 bits) + 4 bits padding
    0x09, 0x39,         //   Usage (Hat switch)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x07,         //   Logical Maximum (7)
    0x35, 0x00,         //   Physical Minimum (0)
    0x46, 0x3B, 0x01,   //   Physical Maximum (315)
    0x65, 0x14,         //   Unit (English Rotation: Degrees)
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x42,         //   Input (Data, Variable, Absolute, Null state)
    0x65, 0x00,         //   Unit (None) — reset
    0x75, 0x04,         //   Report Size (4)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x03,         //   Input (Constant)

    0xC0,               // End Collection
};
const uint16_t gamepad_hid_descriptor_len = sizeof(gamepad_hid_descriptor);

// ── Mouse HID descriptor ──────────────────────────────────────────────────────
// 5 buttons + relative X/Y/wheel.
const uint8_t mouse_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x02,         // Usage (Mouse)
    0xA1, 0x01,         // Collection (Application)
    0x09, 0x01,         //   Usage (Pointer)
    0xA1, 0x00,         //   Collection (Physical)

    // 5 buttons
    0x05, 0x09,         //     Usage Page (Button)
    0x19, 0x01,         //     Usage Minimum (1)
    0x29, 0x05,         //     Usage Maximum (5)
    0x15, 0x00,         //     Logical Minimum (0)
    0x25, 0x01,         //     Logical Maximum (1)
    0x75, 0x01,         //     Report Size (1)
    0x95, 0x05,         //     Report Count (5)
    0x81, 0x02,         //     Input (Data, Variable, Absolute)

    // 3 bits padding
    0x75, 0x03,         //     Report Size (3)
    0x95, 0x01,         //     Report Count (1)
    0x81, 0x03,         //     Input (Constant)

    // Relative X, Y, wheel
    0x05, 0x01,         //     Usage Page (Generic Desktop)
    0x09, 0x30,         //     Usage (X)
    0x09, 0x31,         //     Usage (Y)
    0x09, 0x38,         //     Usage (Wheel)
    0x15, 0x81,         //     Logical Minimum (-127)
    0x25, 0x7F,         //     Logical Maximum (127)
    0x75, 0x08,         //     Report Size (8)
    0x95, 0x03,         //     Report Count (3)
    0x81, 0x06,         //     Input (Data, Variable, Relative)

    0xC0,               //   End Collection (Physical)
    0xC0,               // End Collection (Application)
};
const uint16_t mouse_hid_descriptor_len = sizeof(mouse_hid_descriptor);

// ── Joystick HID descriptor ───────────────────────────────────────────────────
// 32 buttons, 8 absolute axes, hat.
const uint8_t joystick_hid_descriptor[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x04,         // Usage (Joystick)
    0xA1, 0x01,         // Collection (Application)

    // 32 buttons
    0x05, 0x09,         //   Usage Page (Button)
    0x19, 0x01,         //   Usage Minimum (1)
    0x29, 0x20,         //   Usage Maximum (32)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x20,         //   Report Count (32)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // 8 axes: X Y Z Rx Ry Rz Slider Dial
    0x05, 0x01,         //   Usage Page (Generic Desktop)
    0x09, 0x30,         //   Usage (X)
    0x09, 0x31,         //   Usage (Y)
    0x09, 0x32,         //   Usage (Z)
    0x09, 0x33,         //   Usage (Rx)
    0x09, 0x34,         //   Usage (Ry)
    0x09, 0x35,         //   Usage (Rz)
    0x09, 0x36,         //   Usage (Slider)
    0x09, 0x37,         //   Usage (Dial)
    0x15, 0x81,         //   Logical Minimum (-127)
    0x25, 0x7F,         //   Logical Maximum (127)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x08,         //   Report Count (8)
    0x81, 0x02,         //   Input (Data, Variable, Absolute)

    // Hat switch (4 bits) + 4 bits padding
    0x09, 0x39,         //   Usage (Hat switch)
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x07,         //   Logical Maximum (7)
    0x35, 0x00,         //   Physical Minimum (0)
    0x46, 0x3B, 0x01,   //   Physical Maximum (315)
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
const uint16_t joystick_hid_descriptor_len = sizeof(joystick_hid_descriptor);

// ── Profile dispatch ──────────────────────────────────────────────────────────

const uint8_t *profile_descriptor(output_profile_t p, uint16_t *len_out) {
    switch (p) {
        case PROFILE_GAMEPAD:
            *len_out = gamepad_hid_descriptor_len;
            return gamepad_hid_descriptor;
        case PROFILE_MOUSE:
            *len_out = mouse_hid_descriptor_len;
            return mouse_hid_descriptor;
        case PROFILE_JOYSTICK:
            *len_out = joystick_hid_descriptor_len;
            return joystick_hid_descriptor;
    }
    *len_out = 0;
    return NULL;
}

uint8_t profile_report_size(output_profile_t p) {
    switch (p) {
        case PROFILE_GAMEPAD:  return sizeof(gamepad_report_t);
        case PROFILE_MOUSE:    return sizeof(mouse_report_t);
        case PROFILE_JOYSTICK: return sizeof(joystick_report_t);
    }
    return 0;
}

uint8_t profile_build_report(output_profile_t p,
                              const merger_state_t *m,
                              uint8_t *buf) {
    switch (p) {
    case PROFILE_GAMEPAD: {
        gamepad_report_t r;
        memset(&r, 0, sizeof(r));
        r.buttons  = (uint16_t)(merger_buttons(m) & 0xFFFF);
        r.lx       = merger_get_output(m, OUT_LX);
        r.ly       = merger_get_output(m, OUT_LY);
        r.rx       = merger_get_output(m, OUT_RX);
        r.ry       = merger_get_output(m, OUT_RY);
        // Triggers: output role returns signed [-127,127]; descriptor uses [0,255]
        r.lt = (uint8_t)((int16_t)merger_get_output(m, OUT_LT) + 127);
        r.rt = (uint8_t)((int16_t)merger_get_output(m, OUT_RT) + 127);
        uint8_t hat = merger_hat(m);
        r.hat = (hat <= 7) ? hat : 0x0F;
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }
    case PROFILE_MOUSE: {
        mouse_report_t r;
        memset(&r, 0, sizeof(r));
        r.buttons = (uint8_t)(merger_buttons(m) & 0x1F);  // 5 buttons
        // Relative deltas use OUT_REL_X/Y/WHEEL bindings and the accumulator.
        // merger_consume_rel reads the accumulator; caller must hold spinlock.
        int8_t rx, ry, rw;
        merger_consume_rel((merger_state_t *)m, &rx, &ry, &rw);
        r.x     = rx;
        r.y     = ry;
        r.wheel = rw;
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }
    case PROFILE_JOYSTICK: {
        joystick_report_t r;
        memset(&r, 0, sizeof(r));
        r.buttons = merger_buttons(m);
        r.x      = merger_get_output(m, OUT_LX);
        r.y      = merger_get_output(m, OUT_LY);
        r.z      = merger_get_output(m, OUT_LT);
        r.rx     = merger_get_output(m, OUT_RX);
        r.ry     = merger_get_output(m, OUT_RY);
        r.rz     = merger_get_output(m, OUT_RT);
        r.slider = merger_get_output(m, OUT_SLIDER);
        r.dial   = merger_get_output(m, OUT_DIAL);
        uint8_t hat = merger_hat(m);
        r.hat = (hat <= 7) ? hat : 0x0F;
        memcpy(buf, &r, sizeof(r));
        return sizeof(r);
    }
    }
    return 0;
}
