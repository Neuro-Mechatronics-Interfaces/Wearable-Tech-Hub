#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// ── Controller / port ─────────────────────────────────────────────────────────
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// ── Debug ─────────────────────────────────────────────────────────────────────
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

// ── Class instances ───────────────────────────────────────────────────────────
#define CFG_TUD_CDC             1   // One CDC port for the shell
#define CFG_TUD_HID             1   // One HID interface (merged output)
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// ── Buffer sizes ──────────────────────────────────────────────────────────────
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256

// Largest report we will ever send (joystick profile = 13 bytes)
#define CFG_TUD_HID_EP_BUFSIZE  64

#endif // TUSB_CONFIG_H
