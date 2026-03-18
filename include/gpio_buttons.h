#pragma once

#ifdef HUB_ENABLE_GPIO_BUTTONS

#include <stdint.h>
#include <stdbool.h>

// ── Default GPIO pin assignments ──────────────────────────────────────────────
// Override any of these in CMakeLists with:
//   target_compile_definitions(mudra_hub PRIVATE HUB_GPIO_BTN_A=20)
//
// All inputs use internal pull-ups; connect button between pin and GND.
// All LED outputs are active-high; wire LED anode → pin → resistor → GND.

#ifndef HUB_GPIO_BTN_A
#define HUB_GPIO_BTN_A      16u   // Pair button
#endif
#ifndef HUB_GPIO_BTN_B
#define HUB_GPIO_BTN_B      17u   // Mode / profile button
#endif
#ifndef HUB_GPIO_LED_PAIR
#define HUB_GPIO_LED_PAIR   14u   // Pairing status LED
#endif
#ifndef HUB_GPIO_LED_DEVICE
#define HUB_GPIO_LED_DEVICE 15u   // Device count / activity LED
#endif

// ── Button events ─────────────────────────────────────────────────────────────
typedef enum {
    BTN_NONE = 0,
    BTN_A_SHORT,    // < 500 ms press:   select next scanned device to pair
    BTN_A_LONG,     // ≥ 1000 ms hold:   toggle scan on / off
    BTN_A_DOUBLE,   // two presses < 400 ms apart:  unpair selected slot
    BTN_B_SHORT,    // < 500 ms press:   cycle output profile
    BTN_B_LONG,     // ≥ 1000 ms hold:   cycle axis merge strategy
} btn_event_t;

// ── LED states ────────────────────────────────────────────────────────────────
// LED_PAIR reflects scanning / pairing progress.
typedef enum {
    LED_PAIR_OFF        = 0,   // idle, no scan
    LED_PAIR_FAST_BLINK = 1,   // scanning (4 Hz)
    LED_PAIR_SLOW_BLINK = 2,   // connecting / pairing (1 Hz)
    LED_PAIR_SOLID      = 3,   // ≥1 device connected
} led_pair_state_t;

// LED_DEVICE blinks N times to show either the number of connected devices
// (normal mode) or the currently selected scan-result index (pairing mode).
// N=0 → LED off.
typedef enum {
    LED_DEV_OFF     = 0,
    LED_DEV_BLINK_N = 1,
} led_dev_state_t;

// ── API ───────────────────────────────────────────────────────────────────────

// Initialise GPIO pins.  Call from core 0 before main loop.
void gpio_buttons_init(void);

// Poll button state and drive LED patterns.  Call every iteration of the
// core 0 main loop (no blocking).
void gpio_buttons_task(void);

// Set the pair LED state (called when BLE state changes).
void gpio_set_led_pair(led_pair_state_t state);

// Set device LED blink count (0 = off, 1-4 = blink N times then pause).
void gpio_set_led_device(int n);

// Retrieve the next pending button event.  Returns BTN_NONE when queue empty.
btn_event_t gpio_next_event(void);

#else  // !HUB_ENABLE_GPIO_BUTTONS — provide empty stubs so main.c compiles unchanged

typedef enum { BTN_NONE=0, BTN_A_SHORT, BTN_A_LONG, BTN_A_DOUBLE,
               BTN_B_SHORT, BTN_B_LONG } btn_event_t;
typedef enum { LED_PAIR_OFF=0, LED_PAIR_FAST_BLINK, LED_PAIR_SLOW_BLINK, LED_PAIR_SOLID } led_pair_state_t;
typedef enum { LED_DEV_OFF=0, LED_DEV_BLINK_N } led_dev_state_t;

static inline void        gpio_buttons_init(void)                  {}
static inline void        gpio_buttons_task(void)                  {}
static inline void        gpio_set_led_pair(led_pair_state_t s)    { (void)s; }
static inline void        gpio_set_led_device(int n)               { (void)n; }
static inline btn_event_t gpio_next_event(void)                    { return BTN_NONE; }

#endif // HUB_ENABLE_GPIO_BUTTONS
