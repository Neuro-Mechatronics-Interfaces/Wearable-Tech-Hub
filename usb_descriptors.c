// usb_descriptors.c — TinyUSB USB descriptor callbacks
//
// We expose a composite device: CDC (shell) + HID (merged gamepad/mouse/joystick).
// The HID report descriptor is dynamic (switches with the output profile), so
// tud_descriptor_hid_report_cb delegates to usb_output_hid_descriptor().

#include "tusb.h"
#include "usb_output.h"

// ── Descriptor constants ──────────────────────────────────────────────────────
#define USB_VID     0xFABBu   // pid.codes open source VID
#define USB_PID     0x0001u   //
#define USB_BCD     0x0200u

// Interface numbers
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

// Endpoint numbers
#define EP_CDC_NOTIFY   0x81u
#define EP_CDC_OUT      0x02u
#define EP_CDC_IN       0x82u
#define EP_HID_IN       0x83u

// ── Device descriptor ─────────────────────────────────────────────────────────
static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&s_device_desc;
}

// ── HID report descriptor ─────────────────────────────────────────────────────
// Dynamic: delegates to usb_output.c so it reflects the current profile.
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
    (void)itf;
    uint16_t len;
    return usb_output_hid_descriptor(&len);
}

// ── Configuration descriptor ──────────────────────────────────────────────────
// Layout: IAD + CDC control + CDC data + HID
#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

// HID descriptor length is fixed at 9 bytes (the interface descriptor entry).
// The report descriptor length is filled in at runtime by tud_hid_descriptor_report_cb.
// For the config descriptor we write 0 as placeholder — TinyUSB fills it from
// the report descriptor callback during enumeration.
#define HID_REPORT_DESC_LEN 0  // filled dynamically

static const uint8_t s_config_desc[] = {
    // Configuration
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // CDC
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EP_CDC_NOTIFY, 8, EP_CDC_OUT, EP_CDC_IN, 64),

    // HID — boot protocol disabled, report protocol, 5 ms poll
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE,
                       HID_REPORT_DESC_LEN, EP_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 5),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_config_desc;
}

// ── String descriptors ────────────────────────────────────────────────────────
static const char *s_strings[] = {
    (const char[]){ 0x09, 0x04 },  // 0: supported language = English
    "Mudra Hub",                   // 1: Manufacturer
    "BLE HID Hub",                 // 2: Product
    "000001",                      // 3: Serial
    "Hub CDC",                     // 4: CDC Interface
    "Hub HID",                     // 5: HID Interface
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[32];

    if (index == 0) {
        memcpy(buf + 1, s_strings[0], 2);
        buf[0] = (TUSB_DESC_STRING << 8) | (2 + 2);
        return buf;
    }

    if (index >= (uint8_t)(sizeof(s_strings) / sizeof(s_strings[0])))
        return NULL;

    const char *str = s_strings[index];
    uint8_t     len = (uint8_t)strlen(str);
    if (len > 31) len = 31;

    for (uint8_t i = 0; i < len; i++)
        buf[1 + i] = str[i];
    buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * len));
    return buf;
}
