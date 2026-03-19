#include "hid_merger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ── Natural semantic for each output role (fallback when binding is AUTO) ─────
static const int8_t s_role_default_sem[OUT_ROLE_COUNT] = {
    [OUT_LX]    = SEM_AXIS_X,
    [OUT_LY]    = SEM_AXIS_Y,
    [OUT_RX]    = SEM_AXIS_RX,
    [OUT_RY]    = SEM_AXIS_RY,
    [OUT_LT]    = SEM_AXIS_Z,
    [OUT_RT]    = SEM_AXIS_RZ,
    [OUT_HAT]   = SEM_HAT,
    [OUT_SLIDER]= SEM_AXIS_SLIDER,
    [OUT_DIAL]  = SEM_AXIS_DIAL,
    [OUT_REL_X] = SEM_REL_X,
    [OUT_REL_Y] = SEM_REL_Y,
    [OUT_WHEEL] = SEM_REL_WHEEL,
};

// ── Usage → semantic ──────────────────────────────────────────────────────────
static int usage_to_sem(uint32_t page, uint32_t usage, bool is_relative) {
    if (page == HID_PAGE_BUTTON) {
        if (usage >= 1 && usage <= (uint32_t)SEM_BUTTON_COUNT)
            return (int)(SEM_BUTTON_BASE + usage - 1);
        return -1;
    }
    if (page == HID_PAGE_GENERIC_DESKTOP) {
        if (is_relative) {
            switch (usage) {
                case HID_USAGE_X:     return SEM_REL_X;
                case HID_USAGE_Y:     return SEM_REL_Y;
                case HID_USAGE_WHEEL: return SEM_REL_WHEEL;
                default: return -1;
            }
        } else {
            switch (usage) {
                case HID_USAGE_X:      return SEM_AXIS_X;
                case HID_USAGE_Y:      return SEM_AXIS_Y;
                case HID_USAGE_Z:      return SEM_AXIS_Z;
                case HID_USAGE_RX:     return SEM_AXIS_RX;
                case HID_USAGE_RY:     return SEM_AXIS_RY;
                case HID_USAGE_RZ:     return SEM_AXIS_RZ;
                case HID_USAGE_SLIDER: return SEM_AXIS_SLIDER;
                case HID_USAGE_DIAL:   return SEM_AXIS_DIAL;
                case HID_USAGE_WHEEL:  return SEM_AXIS_WHEEL;
                case HID_USAGE_HAT:    return SEM_HAT;
                default: return -1;
            }
        }
    }
    return -1;
}

// ── Normalise raw field value to [-127, 127] ──────────────────────────────────
static inline int8_t clamp8(int32_t v) {
    return v >  127 ?  127 : v < -127 ? -127 : (int8_t)v;
}

static int8_t normalise(int32_t raw, int32_t lmin, int32_t lmax) {
    if (lmax <= lmin) return 0;
    int64_t num = (int64_t)(raw - lmin) * 254 - 127 * (int64_t)(lmax - lmin);
    return clamp8((int32_t)(num / (lmax - lmin)));
}

// ── Helpers: find logical range for a sem_id on a given device ────────────────
static void get_logical_range(const device_state_t *dev, int sem_id,
                               int32_t *lmin_out, int32_t *lmax_out) {
    *lmin_out = 0; *lmax_out = 0;
    for (int i = 0; i < dev->field_map_count; i++) {
        if (dev->field_map[i].sem_id == sem_id) {
            *lmin_out = dev->field_map[i].logical_min;
            *lmax_out = dev->field_map[i].logical_max;
            return;
        }
    }
}

static bool device_has_sem(const device_state_t *dev, int sem_id) {
    for (int i = 0; i < dev->field_map_count; i++)
        if (dev->field_map[i].sem_id == sem_id) return true;
    return false;
}

// ── Public API ────────────────────────────────────────────────────────────────

// ── Helper: refresh m->has_mouse from active device flags ─────────────────────
static void refresh_has_mouse(merger_state_t *m) {
    m->has_mouse = false;
    for (int i = 0; i < MAX_DEVICES; i++)
        if (m->devices[i].active && m->devices[i].is_mouse) { m->has_mouse = true; return; }
}

void merger_init(merger_state_t *m) {
    memset(m, 0, sizeof(*m));
    m->axis_strategy    = AXIS_MERGE_ADDITIVE;
    m->mouse_sensitivity = 1;
    for (int i = 0; i < OUT_ROLE_COUNT; i++) {
        m->bindings[i].slot   = BINDING_SLOT_ANY;
        m->bindings[i].sem_id = BINDING_SEM_AUTO;
    }
    for (int i = 0; i < OUT_BTN_COUNT; i++) {
        m->btn_bindings[i].slot    = BINDING_SLOT_ANY;
        m->btn_bindings[i].sem_btn = BINDING_SEM_AUTO;
    }
}

void merger_build_field_map(merger_state_t *m, int slot,
                             const hid_descriptor_t *desc) {
    if (slot < 0 || slot >= MAX_DEVICES) return;
    device_state_t *dev = &m->devices[slot];
    memset(dev->field_map, 0, sizeof(dev->field_map));
    dev->field_map_count = 0;

    for (int i = 0; i < desc->count && dev->field_map_count < MAX_FIELD_MAP_ENTRIES; i++) {
        const hid_field_t *f = &desc->fields[i];
        bool rel = (f->flags & HID_INPUT_RELATIVE) != 0;
        int  sem = usage_to_sem(f->usage_page, f->usage, rel);
        if (sem < 0) continue;

        field_map_entry_t *e = &dev->field_map[dev->field_map_count++];
        e->sem_id      = sem;
        e->bit_offset  = f->bit_offset;
        e->bit_size    = f->bit_size;
        e->is_relative = rel;
        e->is_signed   = (f->logical_min < 0);
        e->logical_min = f->logical_min;
        e->logical_max = f->logical_max;
        e->report_id   = f->report_id;
    }
    dev->is_mouse = (desc->collection_usage == HID_USAGE_MOUSE);
    dev->active   = true;
    refresh_has_mouse(m);
}

void merger_feed_report(merger_state_t *m, int slot,
                         const uint8_t *report, uint16_t len) {
    if (slot < 0 || slot >= MAX_DEVICES || !m->devices[slot].active || len == 0) return;
    device_state_t *dev = &m->devices[slot];

    // Detect report-ID prefix
    bool    uses_ids    = false;
    uint8_t incoming_id = 0;
    for (int i = 0; i < dev->field_map_count; i++)
        if (dev->field_map[i].report_id != 0) { uses_ids = true; break; }

    const uint8_t *body     = report;
    uint16_t       body_len = len;
    if (uses_ids && len > 0) { incoming_id = report[0]; body = report + 1; body_len = len - 1; }

    for (int i = 0; i < dev->field_map_count; i++) {
        field_map_entry_t *e = &dev->field_map[i];
        if (uses_ids && e->report_id != incoming_id) continue;

        int32_t raw = hid_extract_field(body, body_len,
                                         e->bit_offset, e->bit_size, e->is_signed);
        if (e->is_relative) {
            int idx = e->sem_id - SEM_REL_X;
            if (idx >= 0 && idx < 3) m->rel_accum[idx] += raw;
        } else {
            dev->values[e->sem_id] = raw;
        }
    }
}

void merger_disconnect(merger_state_t *m, int slot) {
    if (slot < 0 || slot >= MAX_DEVICES) return;
    memset(&m->devices[slot], 0, sizeof(m->devices[slot]));
    refresh_has_mouse(m);
}

// Internal: resolve one output button role through its binding + merge strategy.
static bool merger_get_button(const merger_state_t *m, output_btn_t btn) {
    if (btn < 0 || btn >= OUT_BTN_COUNT) return false;
    const button_binding_t *b = &m->btn_bindings[btn];
    int idx = (b->sem_btn == BINDING_SEM_AUTO) ? (int)btn : (int)b->sem_btn;
    int sem = SEM_BUTTON_BASE + idx;
    if (idx < 0 || idx >= SEM_BUTTON_COUNT) return false;

    if (b->slot != BINDING_SLOT_ANY) {
        int slot = b->slot;
        if (slot < 0 || slot >= MAX_DEVICES || !m->devices[slot].active) return false;
        return m->devices[slot].values[sem] != 0;
    }

    // Merge across all active devices that expose this semantic button
    switch (m->axis_strategy) {
        case AXIS_MERGE_ADDITIVE:
        case AXIS_MERGE_LAST:
            for (int d = 0; d < MAX_DEVICES; d++) {
                if (!m->devices[d].active || !device_has_sem(&m->devices[d], sem)) continue;
                if (m->devices[d].values[sem]) return true;
            }
            return false;
        case AXIS_MERGE_PRIORITY:
            for (int d = 0; d < MAX_DEVICES; d++) {
                if (!m->devices[d].active || !device_has_sem(&m->devices[d], sem)) continue;
                return m->devices[d].values[sem] != 0;  // first active device wins
            }
            return false;
    }
    return false;
}

uint16_t merger_get_buttons_word(const merger_state_t *m) {
    uint16_t result = 0;
    for (int b = 0; b < OUT_BTN_COUNT; b++) {
        if (merger_get_button(m, (output_btn_t)b))
            result |= (uint16_t)(1u << b);
    }
    return result;
}

int8_t merger_axis(const merger_state_t *m, int sem_id) {
    if (sem_id < 0 || sem_id >= SEM_COUNT) return 0;

    // Relative mouse axes bypass the absolute-value path and use the
    // pre-flushed per-frame deltas from merger_flush_mouse_axes().
    if (sem_id == SEM_REL_X) return m->mouse_delta[0];
    if (sem_id == SEM_REL_Y) return m->mouse_delta[1];

    int32_t sum = 0; bool any = false; int32_t priority = 0;
    int32_t last = 0; bool last_set = false;

    for (int d = 0; d < MAX_DEVICES; d++) {
        const device_state_t *dev = &m->devices[d];
        if (!dev->active || !device_has_sem(dev, sem_id)) continue;
        int32_t lmin, lmax;
        get_logical_range(dev, sem_id, &lmin, &lmax);
        int8_t norm = (lmax > lmin) ? normalise(dev->values[sem_id], lmin, lmax)
                                    : clamp8(dev->values[sem_id]);
        sum += norm;
        if (!any) { priority = norm; any = true; }
        if (norm != 0) { last = norm; last_set = true; }
    }
    switch (m->axis_strategy) {
        case AXIS_MERGE_ADDITIVE: return clamp8(sum);
        case AXIS_MERGE_PRIORITY: return (int8_t)priority;
        case AXIS_MERGE_LAST:     return last_set ? (int8_t)last : 0;
    }
    return 0;
}

int8_t merger_get_output(const merger_state_t *m, output_role_t role) {
    if (role < 0 || role >= OUT_ROLE_COUNT) return 0;

    const axis_binding_t *b = &m->bindings[role];
    int sem_id = (b->sem_id == BINDING_SEM_AUTO)
                 ? (int)s_role_default_sem[role]
                 : (int)b->sem_id;

    if (b->slot == BINDING_SLOT_ANY) {
        // Use global merge logic
        return merger_axis(m, sem_id);
    }

    // Specific device slot
    int slot = b->slot;
    if (slot < 0 || slot >= MAX_DEVICES || !m->devices[slot].active) return 0;
    const device_state_t *dev = &m->devices[slot];
    if (!device_has_sem(dev, sem_id)) return 0;

    int32_t lmin, lmax;
    get_logical_range(dev, sem_id, &lmin, &lmax);
    return (lmax > lmin) ? normalise(dev->values[sem_id], lmin, lmax)
                         : clamp8(dev->values[sem_id]);
}

uint8_t merger_hat(const merger_state_t *m) {
    const axis_binding_t *b = &m->bindings[OUT_HAT];
    int sem_id = (b->sem_id == BINDING_SEM_AUTO) ? SEM_HAT : (int)b->sem_id;

    int start_slot = 0;
    int end_slot   = MAX_DEVICES;
    if (b->slot != BINDING_SLOT_ANY) { start_slot = b->slot; end_slot = b->slot + 1; }

    for (int d = start_slot; d < end_slot; d++) {
        const device_state_t *dev = &m->devices[d];
        if (!dev->active || !device_has_sem(dev, sem_id)) continue;
        int32_t v = dev->values[sem_id];
        if (v != (int32_t)SEM_HAT_CENTER && v >= 0 && v <= 7)
            return (uint8_t)v;
    }
    return SEM_HAT_CENTER;
}

void merger_consume_rel(merger_state_t *m,
                         int8_t *rel_x, int8_t *rel_y, int8_t *rel_wheel) {
    *rel_x     = clamp8(m->rel_accum[0]);
    *rel_y     = clamp8(m->rel_accum[1]);
    *rel_wheel = clamp8(m->rel_accum[2]);
    m->rel_accum[0] = m->rel_accum[1] = m->rel_accum[2] = 0;
}

void merger_flush_mouse_axes(merger_state_t *m) {
    if (!m->has_mouse) {
        m->mouse_delta[0] = m->mouse_delta[1] = 0;
        m->rel_accum[0] = m->rel_accum[1] = m->rel_accum[2] = 0;
        return;
    }
    int s = (m->mouse_sensitivity > 0) ? (int)m->mouse_sensitivity : 1;
    m->mouse_delta[0] = clamp8(m->rel_accum[0] / s);
    m->mouse_delta[1] = clamp8(m->rel_accum[1] / s);
    m->rel_accum[0] = m->rel_accum[1] = m->rel_accum[2] = 0;
}

bool merger_has_mouse(const merger_state_t *m) {
    return m->has_mouse;
}

void merger_set_binding(merger_state_t *m, output_role_t role, int slot, int sem_id) {
    if (role < 0 || role >= OUT_ROLE_COUNT) return;
    m->bindings[role].slot   = (int8_t)slot;
    m->bindings[role].sem_id = (int8_t)sem_id;
}

void merger_set_btn_binding(merger_state_t *m, output_btn_t btn, int slot, int sem_btn) {
    if (btn < 0 || btn >= OUT_BTN_COUNT) return;
    m->btn_bindings[btn].slot    = (int8_t)slot;
    m->btn_bindings[btn].sem_btn = (int8_t)sem_btn;
}

// ── CLI helpers ───────────────────────────────────────────────────────────────

const char *sem_name(int sem_id) {
    static char s_buf[16];
    if (sem_id >= SEM_BUTTON_BASE && sem_id < SEM_BUTTON_BASE + SEM_BUTTON_COUNT) {
        snprintf(s_buf, sizeof(s_buf), "btn%d", sem_id - SEM_BUTTON_BASE + 1);
        return s_buf;
    }
    switch (sem_id) {
        case SEM_AXIS_X:      return "x";
        case SEM_AXIS_Y:      return "y";
        case SEM_AXIS_Z:      return "z";
        case SEM_AXIS_RX:     return "rx";
        case SEM_AXIS_RY:     return "ry";
        case SEM_AXIS_RZ:     return "rz";
        case SEM_AXIS_SLIDER: return "slider";
        case SEM_AXIS_DIAL:   return "dial";
        case SEM_AXIS_WHEEL:  return "wheel";
        case SEM_HAT:         return "hat";
        case SEM_REL_X:       return "relx";
        case SEM_REL_Y:       return "rely";
        case SEM_REL_WHEEL:   return "relwheel";
        default:              return "?";
    }
}

static const char *s_role_names[OUT_ROLE_COUNT] = {
    "lx", "ly", "rx", "ry", "lt", "rt", "hat",
    "slider", "dial", "relx", "rely", "wheel",
};

int output_role_from_name(const char *name) {
    for (int i = 0; i < OUT_ROLE_COUNT; i++)
        if (strcmp(s_role_names[i], name) == 0) return i;
    return -1;
}

int sem_axis_from_name(const char *name) {
    if (strcmp(name, "x")       == 0) return SEM_AXIS_X;
    if (strcmp(name, "y")       == 0) return SEM_AXIS_Y;
    if (strcmp(name, "z")       == 0) return SEM_AXIS_Z;
    if (strcmp(name, "rx")      == 0) return SEM_AXIS_RX;
    if (strcmp(name, "ry")      == 0) return SEM_AXIS_RY;
    if (strcmp(name, "rz")      == 0) return SEM_AXIS_RZ;
    if (strcmp(name, "slider")  == 0) return SEM_AXIS_SLIDER;
    if (strcmp(name, "dial")    == 0) return SEM_AXIS_DIAL;
    if (strcmp(name, "wheel")   == 0) return SEM_AXIS_WHEEL;
    if (strcmp(name, "hat")     == 0) return SEM_HAT;
    if (strcmp(name, "relx")    == 0) return SEM_REL_X;
    if (strcmp(name, "rely")    == 0) return SEM_REL_Y;
    if (strcmp(name, "relwheel")== 0) return SEM_REL_WHEEL;
    return -1;
}

static const char *s_btn_role_names[OUT_BTN_COUNT] = {
    "btn_south", "btn_east",  "btn_west",    "btn_north",
    "btn_l1",    "btn_r1",    "btn_l2d",     "btn_r2d",
    "btn_select","btn_start", "btn_l3",      "btn_r3",
    "btn_home",  "btn_capture",
};

int output_btn_from_name(const char *name) {
    for (int i = 0; i < OUT_BTN_COUNT; i++)
        if (strcmp(s_btn_role_names[i], name) == 0) return i;
    return -1;
}

int sem_btn_from_name(const char *name) {
    // Accept "btn1"…"btn32" → 0-based index
    if (strncmp(name, "btn", 3) != 0) return -1;
    int n = atoi(name + 3);
    if (n < 1 || n > SEM_BUTTON_COUNT) return -1;
    return n - 1;  // 0-based
}

// ── Device field description ──────────────────────────────────────────────────

void merger_describe_device(const merger_state_t *m, int slot,
                             void (*print)(const char *line)) {
    if (slot < 0 || slot >= MAX_DEVICES) return;
    const device_state_t *dev = &m->devices[slot];
    char buf[128];

    if (!dev->active) {
        snprintf(buf, sizeof(buf), "  slot %d: not connected\r\n", slot);
        print(buf);
        return;
    }

    // Group consecutive button entries to print compactly
    int i = 0;
    while (i < dev->field_map_count) {
        const field_map_entry_t *e = &dev->field_map[i];

        // Detect a run of button fields
        if (e->sem_id >= SEM_BUTTON_BASE &&
            e->sem_id < SEM_BUTTON_BASE + SEM_BUTTON_COUNT) {
            int first_btn = e->sem_id - SEM_BUTTON_BASE + 1;
            int last_btn  = first_btn;
            int j = i + 1;
            while (j < dev->field_map_count &&
                   dev->field_map[j].sem_id == SEM_BUTTON_BASE + (last_btn) &&
                   dev->field_map[j].sem_id < SEM_BUTTON_BASE + SEM_BUTTON_COUNT) {
                last_btn++; j++;
            }
            if (last_btn > first_btn) {
                snprintf(buf, sizeof(buf),
                         "    buttons %d-%d: %d bits total at bit %u\r\n",
                         first_btn, last_btn,
                         last_btn - first_btn + 1, e->bit_offset);
            } else {
                snprintf(buf, sizeof(buf),
                         "    button %d: bit %u\r\n",
                         first_btn, e->bit_offset);
            }
            print(buf);
            i = j;
            continue;
        }

        // Non-button field
        const char *type = e->is_relative ? "rel" : e->is_signed ? "s8" : "u8";
        snprintf(buf, sizeof(buf),
                 "    %-10s bit %u  size %u  %s  logical[%d,%d]%s\r\n",
                 sem_name(e->sem_id), e->bit_offset, e->bit_size, type,
                 (int)e->logical_min, (int)e->logical_max,
                 e->report_id ? " (has report ID)" : "");
        print(buf);
        i++;
    }

    // Show which axis roles are bound to this slot
    bool any_bound = false;
    for (int r = 0; r < OUT_ROLE_COUNT; r++) {
        if (m->bindings[r].slot == (int8_t)slot) {
            if (!any_bound) { print("    axis bindings:\r\n"); any_bound = true; }
            int sem = m->bindings[r].sem_id == BINDING_SEM_AUTO
                      ? (int)s_role_default_sem[r] : (int)m->bindings[r].sem_id;
            snprintf(buf, sizeof(buf), "      %s → slot%d.%s\r\n",
                     s_role_names[r], slot, sem_name(sem));
            print(buf);
        }
    }
    // Show which button roles are bound to this slot
    bool any_btn_bound = false;
    for (int b = 0; b < OUT_BTN_COUNT; b++) {
        if (m->btn_bindings[b].slot == (int8_t)slot) {
            if (!any_btn_bound) { print("    button bindings:\r\n"); any_btn_bound = true; }
            int idx = m->btn_bindings[b].sem_btn == BINDING_SEM_AUTO
                      ? b : (int)m->btn_bindings[b].sem_btn;
            snprintf(buf, sizeof(buf), "      %s → slot%d.btn%d\r\n",
                     s_btn_role_names[b], slot, idx + 1);
            print(buf);
        }
    }
}
