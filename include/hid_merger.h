#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hid_parser.h"

// ── Semantic input IDs ────────────────────────────────────────────────────────
// Device-agnostic input slots after parsing each device's HID descriptor.

#define SEM_BUTTON_BASE     0
#define SEM_BUTTON_COUNT    32

#define SEM_AXIS_X          (SEM_BUTTON_BASE + SEM_BUTTON_COUNT + 0)
#define SEM_AXIS_Y          (SEM_AXIS_X + 1)
#define SEM_AXIS_Z          (SEM_AXIS_X + 2)
#define SEM_AXIS_RX         (SEM_AXIS_X + 3)
#define SEM_AXIS_RY         (SEM_AXIS_X + 4)
#define SEM_AXIS_RZ         (SEM_AXIS_X + 5)
#define SEM_AXIS_SLIDER     (SEM_AXIS_X + 6)
#define SEM_AXIS_DIAL       (SEM_AXIS_X + 7)
#define SEM_AXIS_WHEEL      (SEM_AXIS_X + 8)  // absolute scroll wheel

#define SEM_HAT             (SEM_AXIS_X + 9)

#define SEM_REL_X           (SEM_HAT + 1)     // relative (mouse) axes
#define SEM_REL_Y           (SEM_REL_X + 1)
#define SEM_REL_WHEEL       (SEM_REL_X + 2)

#define SEM_COUNT           (SEM_REL_X + 3)

#define SEM_HAT_CENTER      0xFF

// ── Axis merge strategy ───────────────────────────────────────────────────────
typedef enum {
    AXIS_MERGE_ADDITIVE  = 0,  // Sum all device values (clamped)
    AXIS_MERGE_PRIORITY  = 1,  // First active device wins
    AXIS_MERGE_LAST      = 2,  // Last non-zero write wins
} axis_merge_t;

// ── Output axis roles ─────────────────────────────────────────────────────────
// Named output positions in the USB HID report.  Each can be bound to a
// specific device slot + semantic input; unbound roles use axis_strategy.
typedef enum {
    OUT_LX = 0,    // Gamepad left stick X / joystick X
    OUT_LY,        // Gamepad left stick Y / joystick Y
    OUT_RX,        // Gamepad right stick X / joystick Rx
    OUT_RY,        // Gamepad right stick Y / joystick Ry
    OUT_LT,        // Left trigger   (HID Z)
    OUT_RT,        // Right trigger  (HID Rz)
    OUT_HAT,       // Hat switch
    OUT_SLIDER,    // Joystick slider
    OUT_DIAL,      // Joystick dial
    OUT_REL_X,     // Mouse relative X
    OUT_REL_Y,     // Mouse relative Y
    OUT_WHEEL,     // Mouse / joystick wheel
    OUT_ROLE_COUNT,
} output_role_t;

// ── Output button roles ───────────────────────────────────────────────────────
// Positional (layout-based) names so cross-platform mapping makes sense.
// Default binding for OUT_BTN_X is SEM_BUTTON_BASE + X (i.e. HID button X+1).
// All three profiles share the same button-bit ordering:
//   bit 0 = south (A / × / B), bit 1 = east (B / ○ / A), ...
typedef enum {
    OUT_BTN_SOUTH   = 0,  // A (Xbox) / × (PS5) / B (Switch)
    OUT_BTN_EAST    = 1,  // B (Xbox) / ○ (PS5) / A (Switch)
    OUT_BTN_WEST    = 2,  // X (Xbox) / □ (PS5) / Y (Switch)
    OUT_BTN_NORTH   = 3,  // Y (Xbox) / △ (PS5) / X (Switch)
    OUT_BTN_L1      = 4,  // LB / L1 / L
    OUT_BTN_R1      = 5,  // RB / R1 / R
    OUT_BTN_L2_DIG  = 6,  // LT digital / L2 digital / ZL
    OUT_BTN_R2_DIG  = 7,  // RT digital / R2 digital / ZR
    OUT_BTN_SELECT  = 8,  // View / Create / Minus
    OUT_BTN_START   = 9,  // Menu / Options / Plus
    OUT_BTN_L3      = 10, // L3 (stick click) — same on all profiles
    OUT_BTN_R3      = 11,
    OUT_BTN_HOME    = 12, // Guide / PS / Home
    OUT_BTN_CAPTURE = 13, // Share / Touchpad click / Capture
    OUT_BTN_COUNT   = 14,
} output_btn_t;

// Sentinel values shared by both binding types
#define BINDING_SLOT_ANY   -1   // use axis_strategy across all active devices
#define BINDING_SEM_AUTO   -1   // resolve from the role's natural semantic

typedef struct {
    int8_t slot;    // 0–3 or BINDING_SLOT_ANY
    int8_t sem_id;  // SEM_* constant or BINDING_SEM_AUTO
} axis_binding_t;

// Button binding: sem_btn is a 0-based button index (0 = HID Button 1).
// BINDING_SEM_AUTO maps output button N to SEM_BUTTON_BASE + N.
typedef struct {
    int8_t slot;    // 0–3 or BINDING_SLOT_ANY
    int8_t sem_btn; // 0–31 (button index) or BINDING_SEM_AUTO
} button_binding_t;

// ── Per-device field map ──────────────────────────────────────────────────────
typedef struct {
    int      sem_id;
    uint32_t bit_offset;
    uint8_t  bit_size;
    bool     is_relative;
    bool     is_signed;
    int32_t  logical_min;
    int32_t  logical_max;
    uint8_t  report_id;
} field_map_entry_t;

#define MAX_FIELD_MAP_ENTRIES HID_MAX_FIELDS

typedef struct {
    bool              active;
    bool              is_mouse;           // true when HID descriptor is a Mouse collection
    field_map_entry_t field_map[MAX_FIELD_MAP_ENTRIES];
    int               field_map_count;
    int32_t           values[SEM_COUNT];
} device_state_t;

// ── Shared merger state ───────────────────────────────────────────────────────
#define MAX_DEVICES 4

typedef struct {
    device_state_t   devices[MAX_DEVICES];
    axis_merge_t     axis_strategy;
    int32_t          rel_accum[3];                   // accumulated relative deltas (X,Y,wheel)
    axis_binding_t   bindings[OUT_ROLE_COUNT];       // per-axis-role output binding
    button_binding_t btn_bindings[OUT_BTN_COUNT];    // per-button-role output binding
    // ── Mouse support ────────────────────────────────────────────────────────
    bool             has_mouse;                      // any active slot is a mouse
    int8_t           mouse_delta[2];                 // per-frame scaled X/Y (filled by merger_flush_mouse_axes)
    uint8_t          mouse_sensitivity;              // raw delta divisor: 1=full, 2=half, …, 8=slow
} merger_state_t;

// ── API ───────────────────────────────────────────────────────────────────────

void    merger_init(merger_state_t *m);

void    merger_build_field_map(merger_state_t *m, int slot,
                                const hid_descriptor_t *desc);

void    merger_feed_report(merger_state_t *m, int slot,
                            const uint8_t *report, uint16_t len);

void    merger_disconnect(merger_state_t *m, int slot);

// Merged button word: applies per-role bindings and merge strategy.
// Returns a 14-bit mask (bit N = OUT_BTN_N is pressed).
uint16_t merger_get_buttons_word(const merger_state_t *m);

// Raw semantic axis (unbound), normalised to [-127,127].
int8_t  merger_axis(const merger_state_t *m, int sem_id);

// Bound output role value — respects axis_bindings[role] and axis_strategy.
// Returns int8_t in [-127,127] for axes, or hat nibble for OUT_HAT.
int8_t  merger_get_output(const merger_state_t *m, output_role_t role);

// Hat: returns 0-7 or SEM_HAT_CENTER (respects OUT_HAT binding).
uint8_t merger_hat(const merger_state_t *m);

// Consume accumulated relative deltas for the current frame.
void    merger_consume_rel(merger_state_t *m,
                            int8_t *rel_x, int8_t *rel_y, int8_t *rel_wheel);

// Scale accumulated mouse relative deltas by mouse_sensitivity and store the
// result in mouse_delta[].  Clears rel_accum.  Call once per HID output frame,
// under the spinlock, before profile_build_report() / usb_output_send().
// After this call, merger_get_output(m, OUT_REL_X/Y) returns the mouse delta.
void    merger_flush_mouse_axes(merger_state_t *m);

// Returns true if at least one active slot has is_mouse set.
bool    merger_has_mouse(const merger_state_t *m);

// Set an explicit axis binding.  Pass BINDING_SLOT_ANY / BINDING_SEM_AUTO to
// clear a binding back to default behaviour.
void    merger_set_binding(merger_state_t *m, output_role_t role,
                            int slot, int sem_id);

// Set an explicit button binding.  sem_btn is 0-based button index (0 = HID
// Button 1).  Pass BINDING_SLOT_ANY / BINDING_SEM_AUTO for default behaviour.
void    merger_set_btn_binding(merger_state_t *m, output_btn_t btn,
                                int slot, int sem_btn);

// Print a human-readable description of device[slot]'s field map and current
// bindings to the supplied print callback (one line per call).
// safe to call without spinlock from core 0 if core 1 is not writing.
void    merger_describe_device(const merger_state_t *m, int slot,
                                void (*print)(const char *line));

// Convert a SEM_* constant to a short name string ("x", "y", "btn3", …).
// Returns a pointer to a static string; not thread-safe but only used for CLI.
const char *sem_name(int sem_id);

// Convert an output role name string ("lx", "ly", …) to an output_role_t.
// Returns -1 if not found.
int output_role_from_name(const char *name);

// Convert a semantic axis name string ("x", "z", "rx", …) to a SEM_AXIS_*
// constant.  Returns -1 if not found.
int sem_axis_from_name(const char *name);

// Convert an output button role name ("btn_south", "btn_l1", …) to output_btn_t.
// Returns -1 if not found.
int output_btn_from_name(const char *name);

// Convert a semantic button name ("btn1"…"btn32") to a 0-based button index.
// Returns -1 if not found or out of range.
int sem_btn_from_name(const char *name);
