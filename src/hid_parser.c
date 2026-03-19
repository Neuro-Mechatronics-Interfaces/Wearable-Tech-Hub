#include "hid_parser.h"
#include <string.h>
#include <stdio.h>

// ── HID item tag/type constants ───────────────────────────────────────────────
// Short items: byte0 = tag(4) | type(2) | size(2)
// size encoding: 0→0B  1→1B  2→2B  3→4B
// type: 0=Main  1=Global  2=Local

// Main tags
#define TAG_INPUT           0x80u
#define TAG_OUTPUT          0x90u
#define TAG_COLLECTION      0xA0u
#define TAG_FEATURE         0xB0u
#define TAG_END_COLLECTION  0xC0u

// Global tags
#define TAG_USAGE_PAGE      0x04u
#define TAG_LOGICAL_MIN     0x14u
#define TAG_LOGICAL_MAX     0x24u
#define TAG_PHYSICAL_MIN    0x34u
#define TAG_PHYSICAL_MAX    0x44u
#define TAG_UNIT_EXP        0x54u
#define TAG_UNIT            0x64u
#define TAG_REPORT_SIZE     0x74u
#define TAG_REPORT_ID       0x84u
#define TAG_REPORT_COUNT    0x94u
#define TAG_PUSH            0xA4u
#define TAG_POP             0xB4u

// Local tags
#define TAG_USAGE           0x08u
#define TAG_USAGE_MIN       0x18u
#define TAG_USAGE_MAX       0x28u
#define TAG_DESIGNATOR_IDX  0x38u
#define TAG_DESIGNATOR_MIN  0x48u
#define TAG_DESIGNATOR_MAX  0x58u
#define TAG_STRING_IDX      0x78u
#define TAG_STRING_MIN      0x88u
#define TAG_STRING_MAX      0x98u
#define TAG_DELIMITER       0xA8u

// ── Global state (stack entry) ────────────────────────────────────────────────
typedef struct {
    uint32_t usage_page;
    int32_t  logical_min;
    int32_t  logical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t  report_id;
} global_state_t;

#define GLOBAL_STACK_DEPTH 4

// ── Local state (reset after each Main item) ──────────────────────────────────
typedef struct {
    uint32_t usages[HID_MAX_FIELDS];
    int      usage_count;
    uint32_t usage_min;
    uint32_t usage_max;
    bool     has_usage_range;
} local_state_t;

// ── Helper: read item data as unsigned / signed ───────────────────────────────
static uint32_t item_u32(const uint8_t *p, int size) {
    switch (size) {
        case 0: return 0;
        case 1: return p[0];
        case 2: return (uint32_t)(p[0]) | ((uint32_t)(p[1]) << 8);
        case 4: return (uint32_t)(p[0]) | ((uint32_t)(p[1]) << 8)
                     | ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24);
        default: return 0;
    }
}

static int32_t item_s32(const uint8_t *p, int size) {
    uint32_t u = item_u32(p, size);
    // Sign-extend
    switch (size) {
        case 1: return (int32_t)(int8_t)(u & 0xFF);
        case 2: return (int32_t)(int16_t)(u & 0xFFFF);
        case 4: return (int32_t)u;
        default: return 0;
    }
}

// ── Main parser ──────────────────────────────────────────────────────────────
int hid_parse_descriptor(const uint8_t *data, uint16_t len,
                          hid_descriptor_t *desc) {
    memset(desc, 0, sizeof(*desc));

    global_state_t g_stack[GLOBAL_STACK_DEPTH];
    int            g_depth = 0;
    global_state_t g = {0};
    local_state_t  l = {0};

    // Track bit position per report-ID.  Since we don't know all IDs in advance
    // we track a simple per-ID offset table.
    struct { uint8_t id; uint32_t offset; } id_offsets[16];
    int id_count = 0;

    const uint8_t *p   = data;
    const uint8_t *end = data + len;

    while (p < end) {
        uint8_t b0   = *p++;
        uint8_t tag  = b0 & 0xFCu;  // upper 6 bits (tag+type in conventional notation)
        int     size = b0 & 0x03u;
        if (size == 3) size = 4;

        if (p + size > end) break;
        const uint8_t *vp = p;
        p += size;

        switch (tag) {
        // ── Global items ─────────────────────────────────────────────────────
        case TAG_USAGE_PAGE:   g.usage_page    = item_u32(vp, size); break;
        case TAG_LOGICAL_MIN:  g.logical_min   = item_s32(vp, size); break;
        case TAG_LOGICAL_MAX:  g.logical_max   = item_s32(vp, size); break;
        case TAG_REPORT_SIZE:  g.report_size   = item_u32(vp, size); break;
        case TAG_REPORT_COUNT: g.report_count  = item_u32(vp, size); break;
        case TAG_REPORT_ID:    g.report_id     = (uint8_t)item_u32(vp, size); break;
        case TAG_PHYSICAL_MIN:
        case TAG_PHYSICAL_MAX:
        case TAG_UNIT_EXP:
        case TAG_UNIT:
            break; // not needed for our merge logic

        case TAG_PUSH:
            if (g_depth < GLOBAL_STACK_DEPTH)
                g_stack[g_depth++] = g;
            break;
        case TAG_POP:
            if (g_depth > 0)
                g = g_stack[--g_depth];
            break;

        // ── Local items ──────────────────────────────────────────────────────
        case TAG_USAGE:
            if (l.usage_count < HID_MAX_FIELDS) {
                uint32_t u = item_u32(vp, size);
                // Extended usage includes page in high 16 bits
                if (size == 4 && (u >> 16)) {
                    // Embedded page — override g.usage_page for this usage
                    // We ignore the embedded page for simplicity (uncommon).
                }
                l.usages[l.usage_count++] = u & 0xFFFFu;
            }
            break;
        case TAG_USAGE_MIN:
            l.usage_min       = item_u32(vp, size);
            l.has_usage_range = true;
            break;
        case TAG_USAGE_MAX:
            l.usage_max       = item_u32(vp, size);
            l.has_usage_range = true;
            break;

        // ── Main items ───────────────────────────────────────────────────────
        case TAG_COLLECTION: {
            uint32_t ctype = item_u32(vp, size);
            // Application collection (type 0x01): record the first one seen so
            // callers can identify Mouse (0x02), Game Pad (0x05), etc.
            if (ctype == 0x01u && desc->collection_usage == 0 && l.usage_count > 0)
                desc->collection_usage = l.usages[0];
            // Reset local state; no fields emitted
            memset(&l, 0, sizeof(l));
            break;
        }
        case TAG_END_COLLECTION:
            memset(&l, 0, sizeof(l));
            break;

        case TAG_OUTPUT:
        case TAG_FEATURE:
            // We only care about Input items; reset local and move on.
            memset(&l, 0, sizeof(l));
            break;

        case TAG_INPUT: {
            uint32_t flags = item_u32(vp, size);

            // Find or create bit-offset entry for this report ID
            uint32_t *offset_ptr = NULL;
            for (int i = 0; i < id_count; i++) {
                if (id_offsets[i].id == g.report_id) {
                    offset_ptr = &id_offsets[i].offset;
                    break;
                }
            }
            if (!offset_ptr && id_count < 16) {
                id_offsets[id_count].id     = g.report_id;
                id_offsets[id_count].offset = 0;
                offset_ptr = &id_offsets[id_count].offset;
                id_count++;
            }

            uint32_t field_bit_start = offset_ptr ? *offset_ptr : 0;

            if (!(flags & HID_INPUT_CONSTANT)) {
                // Data field — emit one hid_field_t per usage/count slot
                for (uint32_t i = 0; i < g.report_count && desc->count < HID_MAX_FIELDS; i++) {
                    hid_field_t *f = &desc->fields[desc->count];

                    f->usage_page  = g.usage_page;
                    f->logical_min = g.logical_min;
                    f->logical_max = g.logical_max;
                    f->report_id   = g.report_id;
                    f->bit_offset  = field_bit_start + i * g.report_size;
                    f->bit_size    = (uint8_t)g.report_size;
                    f->flags       = (uint8_t)flags;

                    // Resolve usage
                    if (l.has_usage_range) {
                        // Array or variable with usage range
                        uint32_t u = l.usage_min + i;
                        f->usage     = u <= l.usage_max ? u : l.usage_min;
                        f->usage_max = l.usage_max;
                    } else if (i < (uint32_t)l.usage_count) {
                        f->usage     = l.usages[i];
                        f->usage_max = f->usage;
                    } else if (l.usage_count > 0) {
                        // Fewer usages than report count: repeat last
                        f->usage     = l.usages[l.usage_count - 1];
                        f->usage_max = f->usage;
                    } else {
                        f->usage     = 0;
                        f->usage_max = 0;
                    }

                    desc->count++;
                }
            }

            // Advance bit offset by the full width of this item
            if (offset_ptr)
                *offset_ptr += g.report_count * g.report_size;

            // Reset local state
            memset(&l, 0, sizeof(l));
            break;
        }

        default:
            break;
        }
    }

    return desc->count;
}

// ── Bit-field extraction ──────────────────────────────────────────────────────
int32_t hid_extract_field(const uint8_t *report, uint16_t report_len,
                           uint32_t bit_offset, uint8_t bit_size,
                           bool is_signed) {
    if (bit_size == 0 || bit_size > 32) return 0;

    uint32_t value = 0;
    for (uint8_t b = 0; b < bit_size; b++) {
        uint32_t abs_bit  = bit_offset + b;
        uint32_t byte_idx = abs_bit >> 3;
        uint8_t  bit_idx  = abs_bit & 7u;
        if (byte_idx >= report_len) break;
        if ((report[byte_idx] >> bit_idx) & 1u)
            value |= (1u << b);
    }

    if (is_signed && bit_size < 32) {
        // Sign-extend
        uint32_t sign_bit = 1u << (bit_size - 1);
        if (value & sign_bit)
            value |= ~(sign_bit - 1u);
        return (int32_t)value;
    }
    return (int32_t)value;
}
