#include "ble_central.h"
#include "hid_parser.h"
#include "pairing_store.h"
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"

// BTstack — included only in this translation unit, never via the public header
#include "btstack.h"
#include "btstack_event.h"
#include "ble/gatt-service/hids_client.h"

// ── Slot types (internal — not exposed in ble_central.h) ──────────────────────
typedef enum {
    SLOT_IDLE        = 0,
    SLOT_CONNECTING  = 1,
    SLOT_DISCOVERING = 2,
    SLOT_CONNECTED   = 3,
} slot_state_t;

typedef struct {
    slot_state_t     state;
    hci_con_handle_t con_handle;
    uint16_t         hids_cid;
    uint8_t          addr[6];
    uint8_t          addr_type;
    uint8_t          num_instances;
} slot_t;

// ── Module state ──────────────────────────────────────────────────────────────
static merger_state_t  *s_merger      = NULL;
static spin_lock_t     *s_spinlock    = NULL;

static slot_t        s_slots[MAX_DEVICES];
static scan_result_t s_scan[MAX_SCAN_RESULTS];
static int           s_scan_count = 0;
static bool          s_scanning   = false;

// Single global HID descriptor storage buffer shared by BTstack's HIDS client.
// Sized for MAX_DEVICES services × 512 bytes each.
#define HID_DESC_STORAGE_SIZE  (MAX_DEVICES * 512)
static uint8_t s_hid_desc_storage[HID_DESC_STORAGE_SIZE];

// Parsed HID descriptor per device slot (one per service instance; we use
// instance 0 for the field map and iterate others for descriptors command).
static hid_descriptor_t s_hid_desc[MAX_DEVICES];

static btstack_packet_callback_registration_t s_hci_handler_reg;
static btstack_packet_callback_registration_t s_sm_handler_reg;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int find_slot_by_handle(hci_con_handle_t h) {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (s_slots[i].con_handle == h) return i;
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (s_slots[i].state == SLOT_IDLE) return i;
    return -1;
}

static int find_slot_by_addr(const uint8_t *addr) {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (s_slots[i].state != SLOT_IDLE &&
            memcmp(s_slots[i].addr, addr, 6) == 0) return i;
    return -1;
}

static int find_slot_by_hids_cid(uint16_t cid) {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (s_slots[i].hids_cid == cid) return i;
    return -1;
}

static void slot_reset(int slot) {
    memset(&s_slots[slot], 0, sizeof(s_slots[slot]));
    s_slots[slot].state      = SLOT_IDLE;
    s_slots[slot].con_handle = HCI_CON_HANDLE_INVALID;
}

// Parse all HID service instances for a connected slot and build the field map
// from the first instance that has a valid descriptor.
static void parse_and_build_map(int slot) {
    uint8_t n = s_slots[slot].num_instances;
    if (n == 0) n = 1;  // conservative fallback

    bool mapped = false;
    for (uint8_t svc = 0; svc < n; svc++) {
        const uint8_t *data = hids_client_descriptor_storage_get_descriptor_data(
                                  s_slots[slot].hids_cid, svc);
        uint16_t len = hids_client_descriptor_storage_get_descriptor_len(
                           s_slots[slot].hids_cid, svc);

        if (!data || len == 0) {
            printf("[ble] slot %d svc %d: no descriptor\n", slot, svc);
            continue;
        }

        int count = hid_parse_descriptor(data, len, &s_hid_desc[slot]);
        printf("[ble] slot %d svc %d: descriptor %u bytes -> %d fields\n",
               slot, svc, (unsigned)len, count);

        if (!mapped && count > 0) {
            uint32_t irq = spin_lock_blocking(s_spinlock);
            merger_build_field_map(s_merger, slot, &s_hid_desc[slot]);
            spin_unlock(s_spinlock, irq);
            mapped = true;
        }
    }
}

// ── HIDS client callback ──────────────────────────────────────────────────────
static void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                                 uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) return;

    uint8_t sub = hci_event_gattservice_meta_get_subevent_code(packet);

    switch (sub) {
    case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
        uint16_t cid    = gattservice_subevent_hid_service_connected_get_hids_cid(packet);
        uint8_t  status = gattservice_subevent_hid_service_connected_get_status(packet);
        uint8_t  n_inst = gattservice_subevent_hid_service_connected_get_num_instances(packet);
        int      slot   = find_slot_by_hids_cid(cid);
        if (slot < 0) break;

        if (status == ERROR_CODE_SUCCESS) {
            s_slots[slot].num_instances = n_inst;
            s_slots[slot].state         = SLOT_CONNECTED;
            printf("[ble] slot %d: HIDS connected, %d service instance(s)\n", slot, n_inst);
            parse_and_build_map(slot);
        } else {
            printf("[ble] slot %d: HIDS connect failed, status=%d\n", slot, status);
            gap_disconnect(s_slots[slot].con_handle);
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_REPORT: {
        uint16_t       cid  = gattservice_subevent_hid_report_get_hids_cid(packet);
        int            slot = find_slot_by_hids_cid(cid);
        if (slot < 0) break;

        const uint8_t *data = gattservice_subevent_hid_report_get_report(packet);
        uint16_t       dlen = gattservice_subevent_hid_report_get_report_len(packet);
        uint32_t irq = spin_lock_blocking(s_spinlock);
        merger_feed_report(s_merger, slot, data, dlen);
        spin_unlock(s_spinlock, irq);
        break;
    }

    default:
        break;
    }
}

// ── HCI / GAP packet handler ──────────────────────────────────────────────────
static void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {

    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            printf("[ble] HCI working\n");
            ble_central_scan_start();
        }
        break;

    case GAP_EVENT_ADVERTISING_REPORT: {
        bd_addr_t      addr;
        gap_event_advertising_report_get_address(packet, addr);
        bd_addr_type_t atype =
            (bd_addr_type_t)gap_event_advertising_report_get_address_type(packet);

        // Auto-reconnect any previously paired device that appears in scan
        for (int i = 0; i < MAX_DEVICES; i++) {
            uint8_t stored[6]; uint8_t stored_type;
            if (!pairing_store_get_addr(i, stored, &stored_type)) continue;
            if (stored_type != (uint8_t)atype || memcmp(stored, addr, 6) != 0) continue;
            if (find_slot_by_addr(addr) >= 0) break;  // already handling

            int slot = find_free_slot();
            if (slot < 0) break;
            printf("[ble] reconnecting paired device -> slot %d\n", slot);
            memcpy(s_slots[slot].addr, addr, 6);
            s_slots[slot].addr_type  = (uint8_t)atype;
            s_slots[slot].con_handle = HCI_CON_HANDLE_INVALID;
            s_slots[slot].state      = SLOT_CONNECTING;
            gap_connect(addr, atype);
            break;
        }

        // Record HID devices in the scan results list for user inspection.
        // Filter: only show devices advertising the HID service UUID 0x1812.
        const uint8_t *ad     = gap_event_advertising_report_get_data(packet);
        uint8_t        ad_len = gap_event_advertising_report_get_data_length(packet);

        bool is_hid = false;
        ad_context_t it;
        ad_iterator_init(&it, ad_len, ad);
        while (ad_iterator_has_more(&it)) {
            uint8_t type = ad_iterator_get_data_type(&it);
            if (type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS ||
                type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS) {
                uint8_t        dlen = ad_iterator_get_data_len(&it);
                const uint8_t *d    = ad_iterator_get_data(&it);
                for (int k = 0; k + 1 < dlen; k += 2) {
                    if (little_endian_read_16(d, k) ==
                        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE) {
                        is_hid = true; break;
                    }
                }
            }
            ad_iterator_next(&it);
        }
        if (!is_hid) break;

        // Deduplicate
        bool already = false;
        for (int i = 0; i < s_scan_count; i++)
            if (memcmp(s_scan[i].addr, addr, 6) == 0) { already = true; break; }
        if (!already && s_scan_count < MAX_SCAN_RESULTS) {
            scan_result_t *r = &s_scan[s_scan_count++];
            memcpy(r->addr, addr, 6);
            r->addr_type = (uint8_t)atype;
            r->valid     = true;
            r->name[0]   = 0;
            // Extract device name from AD
            ad_iterator_init(&it, ad_len, ad);
            while (ad_iterator_has_more(&it)) {
                uint8_t type = ad_iterator_get_data_type(&it);
                if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
                    type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) {
                    uint8_t        dlen = ad_iterator_get_data_len(&it);
                    const uint8_t *d    = ad_iterator_get_data(&it);
                    int n = dlen < (int)sizeof(r->name) - 1 ? dlen : (int)sizeof(r->name) - 1;
                    memcpy(r->name, d, n);
                    r->name[n] = 0;
                }
                ad_iterator_next(&it);
            }
            char addr_str[18];
            snprintf(addr_str, sizeof(addr_str), "%s", bd_addr_to_str(addr));
            printf("[ble] HID device: %s  \"%s\"\n", addr_str, r->name);
        }
        break;
    }

    case HCI_EVENT_LE_META:
        if (hci_event_le_meta_get_subevent_code(packet) ==
            HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
            uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                printf("[ble] connection failed status=%d\n", status);
                break;
            }
            hci_con_handle_t h = hci_subevent_le_connection_complete_get_connection_handle(packet);
            bd_addr_t addr;
            hci_subevent_le_connection_complete_get_peer_address(packet, addr);

            int slot = find_slot_by_addr(addr);
            if (slot < 0) slot = find_free_slot();
            if (slot < 0) {
                printf("[ble] no free slot -- disconnecting\n");
                gap_disconnect(h);
                break;
            }

            printf("[ble] connected slot=%d handle=%04x\n", slot, h);
            memcpy(s_slots[slot].addr, addr, 6);
            s_slots[slot].con_handle = h;
            s_slots[slot].state      = SLOT_DISCOVERING;

            uint8_t err = hids_client_connect(h, hids_packet_handler,
                                              HID_PROTOCOL_MODE_REPORT,
                                              &s_slots[slot].hids_cid);
            if (err != ERROR_CODE_SUCCESS)
                printf("[ble] hids_client_connect error %d\n", err);

            // Stop scanning when all slots are busy
            bool all_busy = true;
            for (int i = 0; i < MAX_DEVICES; i++)
                if (s_slots[i].state == SLOT_IDLE) { all_busy = false; break; }
            if (all_busy) gap_stop_scan();
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        hci_con_handle_t h = hci_event_disconnection_complete_get_connection_handle(packet);
        int slot = find_slot_by_handle(h);
        if (slot < 0) break;

        printf("[ble] disconnected slot=%d\n", slot);
        uint32_t irq = spin_lock_blocking(s_spinlock);
        merger_disconnect(s_merger, slot);
        spin_unlock(s_spinlock, irq);

        hids_client_disconnect(s_slots[slot].hids_cid);
        slot_reset(slot);

        if (!s_scanning) ble_central_scan_start();
        break;
    }

    default:
        break;
    }
}

// ── SM (pairing) handler ──────────────────────────────────────────────────────
static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != SM_EVENT_JUST_WORKS_REQUEST) return;
    hci_con_handle_t h = sm_event_just_works_request_get_handle(packet);
    printf("[sm] accepting Just Works on handle %04x\n", h);
    sm_just_works_confirm(h);
}

// ── Public API ────────────────────────────────────────────────────────────────

void ble_central_init(merger_state_t *shared_merger, uint32_t spinlock_num) {
    s_merger   = shared_merger;
    s_spinlock = spin_lock_instance(spinlock_num);

    for (int i = 0; i < MAX_DEVICES; i++) slot_reset(i);

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    hids_client_init(s_hid_desc_storage, sizeof(s_hid_desc_storage));

    s_hci_handler_reg.callback = &hci_packet_handler;
    hci_add_event_handler(&s_hci_handler_reg);

    s_sm_handler_reg.callback = &sm_packet_handler;
    sm_add_event_handler(&s_sm_handler_reg);

    // scan_type=0 (passive), interval=0x0030, window=0x0030
    gap_set_scan_parameters(0, 0x0030, 0x0030);
    // conn_scan_interval, conn_scan_window, conn_interval_min, conn_interval_max,
    // conn_latency, supervision_timeout, min_ce_length, max_ce_length
    gap_set_connection_parameters(0x0008, 0x0018, 0, 0, 0x000C, 0x0028, 0, 0);

    hci_power_control(HCI_POWER_ON);
}

void ble_central_run_forever(void) {
    btstack_run_loop_execute();  // never returns
}

void ble_central_scan_start(void) {
    s_scan_count = 0;
    s_scanning   = true;
    gap_start_scan();
    printf("[ble] scan started\n");
}

void ble_central_scan_stop(void) {
    s_scanning = false;
    gap_stop_scan();
    printf("[ble] scan stopped\n");
}

bool ble_central_pair(const char *addr_str) {
    bd_addr_t addr;
    if (!sscanf_bd_addr(addr_str, addr)) return false;
    if (find_slot_by_addr(addr) >= 0) { printf("[ble] already connected\n"); return false; }

    int slot = find_free_slot();
    if (slot < 0) { printf("[ble] no free slot\n"); return false; }

    pairing_store_set_addr(slot, addr, (uint8_t)BD_ADDR_TYPE_LE_PUBLIC);
    memcpy(s_slots[slot].addr, addr, 6);
    s_slots[slot].addr_type  = (uint8_t)BD_ADDR_TYPE_LE_PUBLIC;
    s_slots[slot].con_handle = HCI_CON_HANDLE_INVALID;
    s_slots[slot].state      = SLOT_CONNECTING;
    gap_connect(addr, BD_ADDR_TYPE_LE_PUBLIC);
    return true;
}

bool ble_central_unpair(const char *addr_str) {
    bd_addr_t addr;
    if (!sscanf_bd_addr(addr_str, addr)) return false;
    int slot = find_slot_by_addr(addr);
    pairing_store_remove_addr(addr);
    if (slot >= 0 && s_slots[slot].con_handle != HCI_CON_HANDLE_INVALID)
        gap_disconnect(s_slots[slot].con_handle);
    return true;
}

void ble_central_list(void (*print)(const char *)) {
    char buf[80];
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_slots[i].state == SLOT_IDLE) continue;
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%s", bd_addr_to_str(s_slots[i].addr));
        const char *st = s_slots[i].state == SLOT_CONNECTED   ? "connected"   :
                         s_slots[i].state == SLOT_CONNECTING  ? "connecting"  :
                         s_slots[i].state == SLOT_DISCOVERING ? "discovering" : "?";
        snprintf(buf, sizeof(buf), "slot%d  %s  %s  (%d svc)\r\n",
                 i, addr_str, st, (int)s_slots[i].num_instances);
        print(buf);
    }
}

void ble_central_format_addr(const uint8_t *addr, char out[18]) {
    // bd_addr_to_str takes a bd_addr_t (uint8_t[6]) and returns a static string
    snprintf(out, 18, "%s", bd_addr_to_str(addr));
}

int  ble_central_scan_result_count(void)            { return s_scan_count; }
const scan_result_t *ble_central_scan_result_at(int idx) {
    return (idx >= 0 && idx < s_scan_count) ? &s_scan[idx] : NULL;
}
