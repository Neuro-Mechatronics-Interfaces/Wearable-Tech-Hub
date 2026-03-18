#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"

#include "hid_merger.h"
#include "ble_central.h"
#include "output_profiles.h"
#include "usb_output.h"
#include "pairing_store.h"
#include "gpio_buttons.h"

#include "tusb.h"

// ── Spinlock for shared merger access between cores ────────────────────────────
#define HUB_SPINLOCK_ID  15

// ── Shared merger state ───────────────────────────────────────────────────────
static merger_state_t s_merger;

// ── GPIO pairing state (used by gpio event handler) ───────────────────────────
static int  s_scan_select_idx = -1;   // which scan result is "selected" by button
static bool s_scanning        = false;

// ── Core 1: BLE stack ─────────────────────────────────────────────────────────
static void core1_entry(void) {
    multicore_lockout_victim_init();  // allows core 0 to pause us during flash writes
    ble_central_init(&s_merger, HUB_SPINLOCK_ID);
    ble_central_run_forever();  // never returns
}

// ── CDC shell ─────────────────────────────────────────────────────────────────
#define CDC_LINE_MAX 160

static char s_line[CDC_LINE_MAX];
static int  s_line_len = 0;

static void cdc_puts(const char *s) {
    if (!tud_cdc_connected()) return;
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

// ── CDC commands ──────────────────────────────────────────────────────────────

static void cmd_help(void) {
    cdc_puts(
        "Commands:\r\n"
        "  help\r\n"
        "  status\r\n"
        "  scan [on|off]\r\n"
        "  pair <AA:BB:CC:DD:EE:FF>\r\n"
        "  unpair <AA:BB:CC:DD:EE:FF>\r\n"
        "  list\r\n"
        "  devices                          describe axes/buttons per connected device\r\n"
        "  profile gamepad|mouse|joystick   switch USB HID output profile\r\n"
        "  axis_merge add|priority|last     axis merge strategy\r\n"
        "  bind show                        show current axis output bindings\r\n"
        "  bind <role> <slot> <axis>        e.g.  bind lx 0 x\r\n"
        "  bind <role> * <axis>             bind role to ANY slot (use merge strategy)\r\n"
        "  bind <role> default              reset role to default binding\r\n"
        "  bind reset                       reset ALL bindings to defaults\r\n"
        "  roles                            list valid output role names\r\n"
        "  axes                             list valid axis names\r\n"
        "  reset                            software reboot\r\n"
    );
}

static void cmd_status(void) {
    char buf[100];
    const char *merge_s =
        s_merger.axis_strategy == AXIS_MERGE_ADDITIVE ? "additive" :
        s_merger.axis_strategy == AXIS_MERGE_PRIORITY ? "priority" : "last";
    const char *prof_s =
        usb_output_get_profile() == PROFILE_GAMEPAD  ? "gamepad"  :
        usb_output_get_profile() == PROFILE_MOUSE    ? "mouse"    : "joystick";
    snprintf(buf, sizeof(buf), "profile: %-10s  axis_merge: %s\r\n", prof_s, merge_s);
    cdc_puts(buf);
    ble_central_list(cdc_puts);

    int n = ble_central_scan_result_count();
    if (n > 0) {
        snprintf(buf, sizeof(buf), "scan results: %d\r\n", n);
        cdc_puts(buf);
        for (int i = 0; i < n; i++) {
            const scan_result_t *r = ble_central_scan_result_at(i);
            if (!r) continue;
            char addr_str[18]; ble_central_format_addr(r->addr, addr_str);
            snprintf(buf, sizeof(buf), "  [%d] %s  \"%s\"\r\n", i, addr_str, r->name);
            cdc_puts(buf);
        }
    }
}

static void cmd_devices(void) {
    char buf[80];
    bool any = false;
    // Read merger state without spinlock (reading from core 0 is safe here since
    // we only read stable active/field_map data, not live values).
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!s_merger.devices[i].active) continue;
        any = true;
        snprintf(buf, sizeof(buf), "slot %d:\r\n", i);
        cdc_puts(buf);
        merger_describe_device(&s_merger, i, cdc_puts);
    }
    if (!any) cdc_puts("no devices connected\r\n");
}

static void cmd_bind_show(void) {
    char buf[100];
    static const char *role_names[] = {
        "lx","ly","rx","ry","lt","rt","hat","slider","dial","relx","rely","wheel"
    };
    cdc_puts("output role   slot   axis\r\n");
    cdc_puts("──────────────────────────\r\n");
    for (int r = 0; r < OUT_ROLE_COUNT; r++) {
        const axis_binding_t *b = &s_merger.bindings[r];
        char slot_s[8];
        if (b->slot == BINDING_SLOT_ANY)
            snprintf(slot_s, sizeof(slot_s), "*");
        else
            snprintf(slot_s, sizeof(slot_s), "%d", (int)b->slot);

        const char *axis_s;
        if (b->sem_id == BINDING_SEM_AUTO)
            axis_s = "(default)";
        else
            axis_s = sem_name(b->sem_id);

        snprintf(buf, sizeof(buf), "  %-10s  %-5s  %s\r\n",
                 role_names[r], slot_s, axis_s);
        cdc_puts(buf);
    }
}

// Process a line from CDC input
static void process_line(char *line) {
    // Trim trailing whitespace
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' '))
        line[--len] = 0;
    if (len == 0) return;

    // Tokenise — up to 5 tokens
    char *argv[5];
    int   argc = 0;
    char *tok  = strtok(line, " \t");
    while (tok && argc < 5) { argv[argc++] = tok; tok = strtok(NULL, " \t"); }
    if (argc == 0) return;

    if (strcmp(argv[0], "help") == 0) {
        cmd_help();

    } else if (strcmp(argv[0], "status") == 0) {
        cmd_status();

    } else if (strcmp(argv[0], "scan") == 0) {
        if (argc < 2 || strcmp(argv[1], "on") == 0) {
            ble_central_scan_start();
            s_scanning = true;
            gpio_set_led_pair(LED_PAIR_FAST_BLINK);
        } else {
            ble_central_scan_stop();
            s_scanning = false;
            gpio_set_led_pair(LED_PAIR_OFF);
        }

    } else if (strcmp(argv[0], "pair") == 0) {
        if (argc < 2) { cdc_puts("usage: pair <addr>\r\n"); return; }
        if (ble_central_pair(argv[1])) {
            cdc_puts("pairing initiated\r\n");
            gpio_set_led_pair(LED_PAIR_SLOW_BLINK);
        } else {
            cdc_puts("failed\r\n");
        }

    } else if (strcmp(argv[0], "unpair") == 0) {
        if (argc < 2) { cdc_puts("usage: unpair <addr>\r\n"); return; }
        ble_central_unpair(argv[1]);
        cdc_puts("ok\r\n");

    } else if (strcmp(argv[0], "list") == 0) {
        ble_central_list(cdc_puts);

    } else if (strcmp(argv[0], "devices") == 0) {
        cmd_devices();

    } else if (strcmp(argv[0], "profile") == 0) {
        if (argc < 2) { cdc_puts("usage: profile gamepad|mouse|joystick\r\n"); return; }
        output_profile_t p;
        if      (strcmp(argv[1], "gamepad")  == 0) p = PROFILE_GAMEPAD;
        else if (strcmp(argv[1], "mouse")    == 0) p = PROFILE_MOUSE;
        else if (strcmp(argv[1], "joystick") == 0) p = PROFILE_JOYSTICK;
        else { cdc_puts("unknown profile\r\n"); return; }
        usb_output_set_profile(p);
        cdc_puts("ok\r\n");

    } else if (strcmp(argv[0], "axis_merge") == 0) {
        if (argc < 2) { cdc_puts("usage: axis_merge add|priority|last\r\n"); return; }
        spin_lock_t *sl = spin_lock_instance(HUB_SPINLOCK_ID);
        uint32_t irq    = spin_lock_blocking(sl);
        if      (strcmp(argv[1], "add")      == 0) s_merger.axis_strategy = AXIS_MERGE_ADDITIVE;
        else if (strcmp(argv[1], "priority") == 0) s_merger.axis_strategy = AXIS_MERGE_PRIORITY;
        else if (strcmp(argv[1], "last")     == 0) s_merger.axis_strategy = AXIS_MERGE_LAST;
        spin_unlock(sl, irq);
        cdc_puts("ok\r\n");

    } else if (strcmp(argv[0], "roles") == 0) {
        cdc_puts("lx  ly  rx  ry  lt  rt  hat  slider  dial  relx  rely  wheel\r\n");

    } else if (strcmp(argv[0], "axes") == 0) {
        cdc_puts("x  y  z  rx  ry  rz  slider  dial  wheel  hat  relx  rely  relwheel\r\n");

    } else if (strcmp(argv[0], "bind") == 0) {
        if (argc < 2) { cdc_puts("usage: bind show | bind reset | bind <role> <slot|*> <axis> | bind <role> default\r\n"); return; }

        if (strcmp(argv[1], "show") == 0) {
            cmd_bind_show();
            return;
        }
        if (strcmp(argv[1], "reset") == 0) {
            spin_lock_t *sl = spin_lock_instance(HUB_SPINLOCK_ID);
            uint32_t irq    = spin_lock_blocking(sl);
            for (int r = 0; r < OUT_ROLE_COUNT; r++)
                merger_set_binding(&s_merger, (output_role_t)r,
                                   BINDING_SLOT_ANY, BINDING_SEM_AUTO);
            spin_unlock(sl, irq);
            cdc_puts("all bindings reset\r\n");
            return;
        }

        // bind <role> <slot|*> <axis>   OR   bind <role> default
        if (argc < 3) { cdc_puts("usage: bind <role> <slot|*> <axis>\r\n"); return; }

        int role = output_role_from_name(argv[1]);
        if (role < 0) { cdc_puts("unknown role — type 'roles'\r\n"); return; }

        int slot, sem_id;
        if (strcmp(argv[2], "default") == 0) {
            slot   = BINDING_SLOT_ANY;
            sem_id = BINDING_SEM_AUTO;
        } else {
            if (argc < 4) { cdc_puts("usage: bind <role> <slot|*> <axis>\r\n"); return; }
            slot = (strcmp(argv[2], "*") == 0) ? BINDING_SLOT_ANY : atoi(argv[2]);
            if (slot != BINDING_SLOT_ANY && (slot < 0 || slot >= MAX_DEVICES)) {
                cdc_puts("slot must be 0-3 or *\r\n"); return;
            }
            sem_id = sem_axis_from_name(argv[3]);
            if (sem_id < 0) { cdc_puts("unknown axis — type 'axes'\r\n"); return; }
        }

        spin_lock_t *sl = spin_lock_instance(HUB_SPINLOCK_ID);
        uint32_t irq    = spin_lock_blocking(sl);
        merger_set_binding(&s_merger, (output_role_t)role, slot, sem_id);
        spin_unlock(sl, irq);
        cdc_puts("ok\r\n");

    } else if (strcmp(argv[0], "reset") == 0) {
        cdc_puts("resetting...\r\n");
        sleep_ms(50);
        watchdog_enable(1, false);
        while (1);

    } else {
        cdc_puts("unknown command — type 'help'\r\n");
    }
}

static void cdc_shell_task(void) {
    if (!tud_cdc_available()) return;
    while (tud_cdc_available()) {
        char c = (char)tud_cdc_read_char();
        if (c == '\n' || c == '\r') {
            s_line[s_line_len] = 0;
            process_line(s_line);
            s_line_len = 0;
        } else if (c == '\b' || c == 127) {
            if (s_line_len > 0) s_line_len--;
        } else if (s_line_len < CDC_LINE_MAX - 1) {
            s_line[s_line_len++] = c;
        }
    }
}

// ── GPIO button event handler ─────────────────────────────────────────────────
static void gpio_event_task(void) {
    btn_event_t ev = gpio_next_event();
    if (ev == BTN_NONE) return;

    switch (ev) {
    case BTN_A_SHORT:
        // Cycle selection through available scan results
        if (ble_central_scan_result_count() > 0) {
            s_scan_select_idx = (s_scan_select_idx + 1)
                                % ble_central_scan_result_count();
            gpio_set_led_device(s_scan_select_idx + 1);  // blink count = slot+1
        }
        break;

    case BTN_A_LONG:
        // Toggle scan
        if (s_scanning) {
            ble_central_scan_stop();
            s_scanning = false;
            gpio_set_led_pair(LED_PAIR_OFF);
        } else {
            s_scan_select_idx = -1;
            ble_central_scan_start();
            s_scanning = true;
            gpio_set_led_pair(LED_PAIR_FAST_BLINK);
        }
        break;

    case BTN_A_DOUBLE:
        // Pair the currently selected scan result
        if (s_scan_select_idx >= 0 &&
            s_scan_select_idx < ble_central_scan_result_count()) {
            const scan_result_t *r =
                ble_central_scan_result_at(s_scan_select_idx);
            if (r) {
                char addr_str[18];
                ble_central_format_addr(r->addr, addr_str);
                if (ble_central_pair(addr_str))
                    gpio_set_led_pair(LED_PAIR_SLOW_BLINK);
            }
        }
        break;

    case BTN_B_SHORT: {
        // Cycle output profile
        output_profile_t p = usb_output_get_profile();
        p = (output_profile_t)((p + 1) % 3);
        usb_output_set_profile(p);
        // Blink device LED to confirm new profile index (1=gamepad, 2=mouse, 3=joystick)
        gpio_set_led_device((int)p + 1);
        break;
    }
    case BTN_B_LONG: {
        // Cycle axis merge strategy
        spin_lock_t *sl = spin_lock_instance(HUB_SPINLOCK_ID);
        uint32_t irq    = spin_lock_blocking(sl);
        s_merger.axis_strategy =
            (axis_merge_t)((s_merger.axis_strategy + 1) % 3);
        spin_unlock(sl, irq);
        break;
    }
    default:
        break;
    }
}

// ── LED status updater ────────────────────────────────────────────────────────
#define LED_UPDATE_INTERVAL_MS 200

static uint32_t s_led_update_ms = 0;

static void led_status_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_led_update_ms < LED_UPDATE_INTERVAL_MS) return;
    s_led_update_ms = now;

    // Count active devices
    int connected = 0;
    for (int i = 0; i < MAX_DEVICES; i++)
        if (s_merger.devices[i].active) connected++;

    // Pair LED: solid if any connected, fast blink if scanning, else off
    if (connected > 0)
        gpio_set_led_pair(LED_PAIR_SOLID);
    else if (s_scanning)
        gpio_set_led_pair(LED_PAIR_FAST_BLINK);
    else
        gpio_set_led_pair(LED_PAIR_OFF);

    // Device LED: show connected count (unless pairing mode is showing selection)
    if (!s_scanning)
        gpio_set_led_device(connected);
}

// ── HID report dispatch ───────────────────────────────────────────────────────
#define HID_REPORT_INTERVAL_MS 4

static uint32_t s_last_report_ms = 0;

static void hid_dispatch_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_last_report_ms < HID_REPORT_INTERVAL_MS) return;
    s_last_report_ms = now;
    usb_output_send(&s_merger, HUB_SPINLOCK_ID);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(void) {
    if (cyw43_arch_init() != 0) {
        while (true) { sleep_ms(200); }
    }

    pairing_store_init();
    merger_init(&s_merger);
    spin_lock_claim(HUB_SPINLOCK_ID);

    gpio_buttons_init();   // no-op when HUB_ENABLE_GPIO_BUTTONS not defined

    multicore_launch_core1(core1_entry);
    usb_output_init();

    while (true) {
        usb_output_task();    // tud_task()
        cdc_shell_task();
        gpio_buttons_task();  // button debounce + LED patterns
        gpio_event_task();    // act on any queued button events
        led_status_task();    // keep LED states in sync with hub state
        hid_dispatch_task();
    }

    return 0;
}
