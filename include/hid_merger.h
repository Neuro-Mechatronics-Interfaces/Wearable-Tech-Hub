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

// ── Output roles ──────────────────────────────────────────────────────────────
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

// Sentinel values for axis_binding_t fields
#define BINDING_SLOT_ANY   -1   // use axis_strategy across all active devices
#define BINDING_SEM_AUTO   -1   // resolve from the role's natural semantic

typedef struct {
    int8_t slot;    // 0–3 or BINDING_SLOT_ANY
    int8_t sem_id;  // SEM_* constant or BINDING_SEM_AUTO
} axis_binding_t;

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
    field_map_entry_t field_map[MAX_FIELD_MAP_ENTRIES];
    int               field_map_count;
    int32_t           values[SEM_COUNT];
} device_state_t;

// ── Shared merger state ───────────────────────────────────────────────────────
#define MAX_DEVICES 4

typedef struct {
    device_state_t devices[MAX_DEVICES];
    axis_merge_t   axis_strategy;
    int32_t        rel_accum[3];              // accumulated relative deltas
    axis_binding_t bindings[OUT_ROLE_COUNT];  // per-role output binding
} merger_state_t;

// ── API ───────────────────────────────────────────────────────────────────────

void    merger_init(merger_state_t *m);

void    merger_build_field_map(merger_state_t *m, int slot,
                                const hid_descriptor_t *desc);

void    merger_feed_report(merger_state_t *m, int slot,
                            const uint8_t *report, uint16_t len);

void    merger_disconnect(merger_state_t *m, int slot);

// Merged button state: OR across all active devices, 32 bits.
uint32_t merger_buttons(const merger_state_t *m);

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

// Set an explicit axis binding.  Pass BINDING_SLOT_ANY / BINDING_SEM_AUTO to
// clear a binding back to default behaviour.
void    merger_set_binding(merger_state_t *m, output_role_t role,
                            int slot, int sem_id);

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
