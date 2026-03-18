#include "gpio_buttons.h"

#ifdef HUB_ENABLE_GPIO_BUTTONS

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <string.h>

// ── Timing constants (milliseconds) ───────────────────────────────────────────
#define DEBOUNCE_MS         20u
#define SHORT_THRESHOLD_MS  500u
#define LONG_THRESHOLD_MS   1000u
#define DOUBLE_GAP_MS       400u    // max gap between two presses for double-tap

// ── LED timing ────────────────────────────────────────────────────────────────
#define LED_FAST_HALF_MS    125u    // 4 Hz → 250 ms period → 125 ms half
#define LED_SLOW_HALF_MS    500u    // 1 Hz → 1000 ms period → 500 ms half
#define LED_BLINK_ON_MS     150u    // on-time per blink pulse
#define LED_BLINK_OFF_MS    200u    // off between pulses
#define LED_BLINK_PAUSE_MS  1200u   // pause between blink groups

// ── Event queue ───────────────────────────────────────────────────────────────
#define EVT_QUEUE_SIZE 8
static btn_event_t s_queue[EVT_QUEUE_SIZE];
static int s_q_head = 0, s_q_tail = 0;

static void enqueue(btn_event_t e) {
    int next = (s_q_tail + 1) % EVT_QUEUE_SIZE;
    if (next != s_q_head) { s_queue[s_q_tail] = e; s_q_tail = next; }
}

btn_event_t gpio_next_event(void) {
    if (s_q_head == s_q_tail) return BTN_NONE;
    btn_event_t e = s_queue[s_q_head];
    s_q_head = (s_q_head + 1) % EVT_QUEUE_SIZE;
    return e;
}

// ── Per-button state machine ──────────────────────────────────────────────────
typedef enum {
    BS_IDLE,
    BS_PRESSED,     // button down, timing
    BS_RELEASED,    // just released, waiting to classify short vs double
    BS_WAIT_DOUBLE, // first release, waiting for second press
} btn_state_t;

typedef struct {
    uint32_t    gpio;
    btn_event_t ev_short;
    btn_event_t ev_long;
    btn_event_t ev_double;

    btn_state_t state;
    uint32_t    press_ms;       // when button went down
    uint32_t    release_ms;     // when button was last released
    bool        long_fired;     // long-press event already sent
} btn_ctx_t;

static btn_ctx_t s_btns[2];

static void btn_task_one(btn_ctx_t *b, uint32_t now_ms) {
    bool pressed = !gpio_get(b->gpio);  // active-low (pull-up + GND)

    switch (b->state) {
    case BS_IDLE:
        if (pressed) { b->state = BS_PRESSED; b->press_ms = now_ms; b->long_fired = false; }
        break;

    case BS_PRESSED:
        if (!pressed) {
            // Button released — was it a long press already?
            if (b->long_fired) { b->state = BS_IDLE; break; }
            b->release_ms = now_ms;
            b->state = BS_WAIT_DOUBLE;
        } else if (!b->long_fired && (now_ms - b->press_ms) >= LONG_THRESHOLD_MS) {
            enqueue(b->ev_long);
            b->long_fired = true;
        }
        break;

    case BS_WAIT_DOUBLE:
        if (pressed) {
            // Second press within double-gap → double-tap
            if ((now_ms - b->release_ms) < DOUBLE_GAP_MS) {
                enqueue(b->ev_double);
                b->state = BS_PRESSED;   // consume the second press
                b->press_ms = now_ms;
                b->long_fired = true;    // prevent extra events on release
            }
        } else if ((now_ms - b->release_ms) >= DOUBLE_GAP_MS) {
            // Timeout — classify as short press
            enqueue(b->ev_short);
            b->state = BS_IDLE;
        }
        break;

    default:
        b->state = BS_IDLE;
        break;
    }
}

// ── LED state ─────────────────────────────────────────────────────────────────
static led_pair_state_t s_led_pair_state = LED_PAIR_OFF;
static int              s_led_dev_n      = 0;  // blink count (0 = off)

// Internal LED driver state
static uint32_t s_led_pair_toggle_ms = 0;
static bool     s_led_pair_level     = false;

static uint32_t s_led_dev_next_ms   = 0;
static int      s_led_dev_blink_cnt = 0;  // pulses remaining in current group
static bool     s_led_dev_on        = false;

static void led_pair_task(uint32_t now_ms) {
    switch (s_led_pair_state) {
    case LED_PAIR_OFF:
        gpio_put(HUB_GPIO_LED_PAIR, false);
        break;
    case LED_PAIR_SOLID:
        gpio_put(HUB_GPIO_LED_PAIR, true);
        break;
    case LED_PAIR_FAST_BLINK:
    case LED_PAIR_SLOW_BLINK: {
        uint32_t half = (s_led_pair_state == LED_PAIR_FAST_BLINK)
                        ? LED_FAST_HALF_MS : LED_SLOW_HALF_MS;
        if ((now_ms - s_led_pair_toggle_ms) >= half) {
            s_led_pair_level        = !s_led_pair_level;
            s_led_pair_toggle_ms    = now_ms;
            gpio_put(HUB_GPIO_LED_PAIR, s_led_pair_level);
        }
        break;
    }
    }
}

static void led_dev_task(uint32_t now_ms) {
    int n = s_led_dev_n;
    if (n <= 0) { gpio_put(HUB_GPIO_LED_DEVICE, false); return; }

    if (now_ms < s_led_dev_next_ms) return;

    if (s_led_dev_blink_cnt == 0) {
        // Start a new blink group
        s_led_dev_blink_cnt = n;
        s_led_dev_on        = false;
    }

    if (!s_led_dev_on) {
        // Turn on for one pulse
        gpio_put(HUB_GPIO_LED_DEVICE, true);
        s_led_dev_on      = true;
        s_led_dev_next_ms = now_ms + LED_BLINK_ON_MS;
    } else {
        // Turn off
        gpio_put(HUB_GPIO_LED_DEVICE, false);
        s_led_dev_on = false;
        s_led_dev_blink_cnt--;
        if (s_led_dev_blink_cnt > 0)
            s_led_dev_next_ms = now_ms + LED_BLINK_OFF_MS;  // inter-pulse gap
        else
            s_led_dev_next_ms = now_ms + LED_BLINK_PAUSE_MS; // end-of-group pause
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void gpio_buttons_init(void) {
    // Button A
    s_btns[0] = (btn_ctx_t){ .gpio = HUB_GPIO_BTN_A,
                              .ev_short = BTN_A_SHORT,
                              .ev_long  = BTN_A_LONG,
                              .ev_double= BTN_A_DOUBLE };
    // Button B
    s_btns[1] = (btn_ctx_t){ .gpio = HUB_GPIO_BTN_B,
                              .ev_short = BTN_B_SHORT,
                              .ev_long  = BTN_B_LONG,
                              .ev_double= BTN_NONE };  // no double-tap for B

    for (int i = 0; i < 2; i++) {
        gpio_init(s_btns[i].gpio);
        gpio_set_dir(s_btns[i].gpio, GPIO_IN);
        gpio_pull_up(s_btns[i].gpio);
    }

    gpio_init(HUB_GPIO_LED_PAIR);   gpio_set_dir(HUB_GPIO_LED_PAIR,   GPIO_OUT);
    gpio_init(HUB_GPIO_LED_DEVICE); gpio_set_dir(HUB_GPIO_LED_DEVICE, GPIO_OUT);

    gpio_put(HUB_GPIO_LED_PAIR,   false);
    gpio_put(HUB_GPIO_LED_DEVICE, false);
}

void gpio_buttons_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    btn_task_one(&s_btns[0], now);
    btn_task_one(&s_btns[1], now);
    led_pair_task(now);
    led_dev_task(now);
}

void gpio_set_led_pair(led_pair_state_t state) {
    s_led_pair_state = state;
}

void gpio_set_led_device(int n) {
    s_led_dev_n = n < 0 ? 0 : n > 4 ? 4 : n;
}

#endif // HUB_ENABLE_GPIO_BUTTONS
