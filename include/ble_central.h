#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hid_merger.h"

// ── Limits ────────────────────────────────────────────────────────────────────
#define MAX_SCAN_RESULTS    16

// ── Scan result (advertised BLE HID device) ───────────────────────────────────
// Uses plain uint8_t types so this header is safe to include alongside tusb.h
// without pulling in BTstack (which would conflict on hid_report_type_t).
typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    char    name[32];
    bool    valid;
} scan_result_t;

// ── BLE central module API ────────────────────────────────────────────────────

// Initialise BTstack, register packet handlers, configure SM for Just Works.
// Must be called on core 1 before ble_central_run_forever().
void ble_central_init(merger_state_t *shared_merger, uint32_t spinlock_num);

// Enter the BTstack run loop (never returns).  Call from core 1 after init.
void ble_central_run_forever(void);

// Request a scan start / stop.  Results are printed to the CDC shell.
void ble_central_scan_start(void);
void ble_central_scan_stop(void);

// Initiate connection + pairing to a device by address string "AA:BB:CC:DD:EE:FF".
// Returns false if no free slot or address invalid.
bool ble_central_pair(const char *addr_str);

// Drop connection and remove bond for a given address.
bool ble_central_unpair(const char *addr_str);

// List all connected slots to the supplied printf-style sink.
void ble_central_list(void (*print)(const char *));

// Get number of scan results collected since last scan_start.
int  ble_central_scan_results(scan_result_t *out, int max);

// Retrieve the scan_results array entry for printing.
const scan_result_t *ble_central_scan_result_at(int idx);
int                  ble_central_scan_result_count(void);

// Format a 6-byte BLE address as "AA:BB:CC:DD:EE:FF\0" into out (18 bytes).
void ble_central_format_addr(const uint8_t *addr, char out[18]);
