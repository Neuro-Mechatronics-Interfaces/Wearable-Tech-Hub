#include "pairing_store.h"
#include <string.h>
#include <stdio.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// ── Flash layout ──────────────────────────────────────────────────────────────
// Reserve the last 4 KiB sector of flash for peer storage.
// BTstack bond keys live elsewhere (managed by BTstack's own TLV); we only
// store the address list used for auto-reconnect.
#define STORE_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define STORE_XIP_ADDR      (XIP_BASE + STORE_FLASH_OFFSET)

#define STORE_MAGIC  0xB1EDAB1Eu

// ── On-flash layout ───────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  addr[6];
    uint8_t  addr_type;
    uint8_t  valid;      // 1 = slot occupied
} peer_record_t;

typedef struct __attribute__((packed)) {
    uint32_t      magic;
    peer_record_t peers[MAX_PAIRED_DEVICES];
} peer_store_t;

// ── RAM cache ─────────────────────────────────────────────────────────────────
static peer_store_t s_store;

// Staging buffer aligned and padded to one flash page (256 bytes).
// flash_range_program requires length to be a multiple of FLASH_PAGE_SIZE.
static uint8_t s_page_buf[FLASH_PAGE_SIZE];

// ── Flash write (called under flash_safe_execute lockout) ─────────────────────
static void do_flash_write(void *param) {
    (void)param;
    // Prepare page buffer: fill with 0xFF then overlay the store struct.
    memset(s_page_buf, 0xFF, sizeof(s_page_buf));
    memcpy(s_page_buf, &s_store, sizeof(s_store));

    flash_range_erase(STORE_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(STORE_FLASH_OFFSET, s_page_buf, sizeof(s_page_buf));
}

static void save_to_flash(void) {
    // 5 s timeout — will fail gracefully if the other core is unresponsive.
    int rc = flash_safe_execute(do_flash_write, NULL, 5000);
    if (rc != PICO_OK) {
        printf("[store] flash_safe_execute failed (%d)\n", rc);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void pairing_store_init(void) {
    // Read directly from XIP (no erase/program here; safe before core 1 starts).
    const peer_store_t *p = (const peer_store_t *)STORE_XIP_ADDR;
    if (p->magic == STORE_MAGIC) {
        memcpy(&s_store, p, sizeof(s_store));
        printf("[store] loaded peer list from flash (offset 0x%06X)\n",
               (unsigned)STORE_FLASH_OFFSET);
    } else {
        memset(&s_store, 0, sizeof(s_store));
        s_store.magic = STORE_MAGIC;
        printf("[store] flash blank — starting with empty peer list\n");
    }
}

void pairing_store_set_addr(int slot, const uint8_t addr[6], uint8_t atype) {
    if (slot < 0 || slot >= MAX_PAIRED_DEVICES) return;
    memcpy(s_store.peers[slot].addr, addr, 6);
    s_store.peers[slot].addr_type = atype;
    s_store.peers[slot].valid     = 1;
    save_to_flash();
}

bool pairing_store_get_addr(int slot, uint8_t addr_out[6], uint8_t *atype_out) {
    if (slot < 0 || slot >= MAX_PAIRED_DEVICES) return false;
    if (!s_store.peers[slot].valid) return false;
    memcpy(addr_out, s_store.peers[slot].addr, 6);
    if (atype_out) *atype_out = s_store.peers[slot].addr_type;
    return true;
}

void pairing_store_remove_addr(const uint8_t addr[6]) {
    bool changed = false;
    for (int i = 0; i < MAX_PAIRED_DEVICES; i++) {
        if (s_store.peers[i].valid &&
            memcmp(s_store.peers[i].addr, addr, 6) == 0) {
            s_store.peers[i].valid = 0;
            changed = true;
            printf("[store] removed peer slot%d\n", i);
        }
    }
    if (changed) save_to_flash();
}

void pairing_store_clear(void) {
    memset(s_store.peers, 0, sizeof(s_store.peers));
    save_to_flash();
    printf("[store] all peer records cleared\n");
}
