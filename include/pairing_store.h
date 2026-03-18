#pragma once
#include <stdint.h>
#include <stdbool.h>

// Flash-backed list of BLE peer addresses to reconnect on boot.
// Stored in the last flash sector using raw hardware/flash.h API.
// flash_safe_execute() is used for erase/program; requires core 1 to have
// called multicore_lockout_victim_init() before any write can occur.

#define MAX_PAIRED_DEVICES  4  // Must match MAX_DEVICES in hid_merger.h

// Initialise from flash.  Call once from core 0 before launching core 1.
void pairing_store_init(void);

// Persist a peer address for a given slot index (0–3).
void pairing_store_set_addr(int slot, const uint8_t addr[6], uint8_t atype);

// Retrieve a stored peer address.  Returns false if slot is empty.
bool pairing_store_get_addr(int slot, uint8_t addr_out[6], uint8_t *atype_out);

// Remove a peer address by value (finds the slot automatically).
void pairing_store_remove_addr(const uint8_t addr[6]);

// Remove all stored peer addresses.
void pairing_store_clear(void);
