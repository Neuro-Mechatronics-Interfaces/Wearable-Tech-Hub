#pragma once
#include <stdint.h>
#include "output_profiles.h"
#include "hid_merger.h"

// Initialise TinyUSB.  Call from core 0 before the main loop.
void usb_output_init(void);

// Set the active output profile.  If the profile changes this triggers a USB
// re-enumeration (D+ toggled for ~5 ms) so the host sees the new descriptor.
void usb_output_set_profile(output_profile_t p);
output_profile_t usb_output_get_profile(void);

// Build and send a HID report from the current merger state.
// Must be called from core 0 with the spinlock held while reading from m.
// Returns true if the report was sent successfully.
bool usb_output_send(merger_state_t *m, uint32_t spinlock_num);

// TinyUSB task — call every iteration of the core 0 main loop.
void usb_output_task(void);

// Return the active HID report descriptor blob (used by TinyUSB descriptor
// callbacks in usb_descriptors.c).
const uint8_t *usb_output_hid_descriptor(uint16_t *len_out);
