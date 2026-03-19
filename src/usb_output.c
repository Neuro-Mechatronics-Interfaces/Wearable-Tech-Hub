#include "usb_output.h"
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "tusb.h"

// ── Module state ──────────────────────────────────────────────────────────────
static output_profile_t s_profile = PROFILE_SWITCH;

// ── Public API ────────────────────────────────────────────────────────────────

void usb_output_init(void) {
    tusb_init();
}

void usb_output_task(void) {
    tud_task();
}

output_profile_t usb_output_get_profile(void) {
    return s_profile;
}

void usb_output_set_profile(output_profile_t p) {
    if (p == s_profile) return;
    s_profile = p;

    // Force USB re-enumeration so the host sees the updated HID descriptor.
    // TinyUSB provides tud_disconnect() / tud_connect() for this purpose.
    printf("[usb] re-enumerating for profile %d\n", (int)p);
    tud_disconnect();
    sleep_ms(20);
    tud_connect();
}

bool usb_output_send(merger_state_t *m, uint32_t spinlock_num) {
    if (!tud_hid_ready()) return false;

    uint8_t buf[16];
    spin_lock_t *sl = spin_lock_instance(spinlock_num);
    uint32_t irq   = spin_lock_blocking(sl);
    uint8_t  len   = profile_build_report(s_profile, m, buf);
    spin_unlock(sl, irq);

    if (len == 0) return false;
    return tud_hid_report(0, buf, len);
}

const uint8_t *usb_output_hid_descriptor(uint16_t *len_out) {
    return profile_descriptor(s_profile, len_out);
}

// ── TinyUSB HID callbacks ─────────────────────────────────────────────────────

// Called when host requests the HID report descriptor.
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    (void)itf; (void)report_id; (void)report_type;
    uint16_t len = 0;
    const uint8_t *desc = usb_output_hid_descriptor(&len);
    if (!desc || len == 0) return 0;
    uint16_t copy = len < reqlen ? len : reqlen;
    memcpy(buffer, desc, copy);
    return copy;
}

// Called when host sends an output / feature report (e.g., LED state).
// We ignore output reports for now.
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                            hid_report_type_t report_type,
                            const uint8_t *buffer, uint16_t bufsize) {
    (void)itf; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}
