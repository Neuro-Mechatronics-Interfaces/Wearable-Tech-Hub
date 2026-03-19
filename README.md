# Wearable Tech Hub — Pico 2 W Firmware

This firmware turns a Raspberry Pi Pico 2 W into a **multi-device BLE HID hub**: it
simultaneously connects to several BLE HID peripherals (Android apps, gaming
controllers, gesture bands, mice, etc.), merges their inputs, and presents a single
merged USB HID device to the host PC or console.

## Design goals

| Goal | Approach |
|---|---|
| Pair ≤4 BLE HID peripherals | BTstack HIDS client, per-connection state |
| OR-merge buttons | Each button bit is OR'd across all live device states |
| Axis merge | Configurable: additive (default for sticks), priority for triggers |
| Mouse support | Relative X/Y accumulated per frame; left-click routed as button |
| Toggle output mode | USB HID report profile: Gamepad / Mouse / Joystick |
| Persistent pairing | BTstack bonding + address list written to Pico flash |
| CDC shell | Add/remove/list paired devices, change output mode |

## Architecture

```
          ┌──────────────────────────────────────────────────────┐
          │                  Pico 2 W                            │
          │                                                      │
  BLE     │  ble_central.c (BTstack HoG client, up to 4 devs)   │
  ─────── │  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
  Device1 │  │ dev[0]   │  │ dev[1]   │  │ dev[2]…  │           │
  Device2 │  │ raw HID  │  │ raw HID  │  │ raw HID  │           │
  Device3 │  └────┬─────┘  └────┬─────┘  └────┬─────┘           │
          │       │             │              │                  │
          │       └─────────────┴──────────────┘                 │
          │                     │                                │
          │            hid_parser.c   (descriptor → field map)   │
          │            hid_merger.c   (OR buttons, additive axes) │
          │                     │                                │
          │            usb_output.c  (TinyUSB HID device)        │
          │                     │                USB             │
          └─────────────────────────────────────────────────────-┘
                                                    │
                                              Host / Console
```

### Dual-core split

| Core | Responsibility |
|------|---------------|
| Core 0 | `tud_task()` (TinyUSB), CDC shell, merged-report dispatch |
| Core 1 | `btstack_run_loop_execute()` (BLE stack, never returns) |

Shared state in `hid_merger.c` is protected by a single hardware spinlock so
both cores can safely access it.

## HID Merge Semantics

### Buttons

Every HID button (usage page 0x09, any usage 1–32) is OR'd across all
connected devices:

```
merged_button[n] = dev[0].button[n] | dev[1].button[n] | … | dev[k].button[n]
```

Both controller[0] and controller[1] pressing "button A" registers as pressed;
both must release for the bit to go to 0.

### Axes (absolute)

Default: **additive with saturation** — useful for sticks (two controllers
can cooperate). Override to `AXIS_MERGE_PRIORITY` (first active device wins)
or `AXIS_MERGE_LAST` (last packet wins) via CDC shell or output profile config.

### Relative axes (mouse X/Y/wheel)

Accumulated per frame: all deltas are summed, scaled by `mouse_sensitivity`,
then clamped to int8 before being emitted in the USB report.

### Hat switch

Priority: first connected device with a non-centre hat wins.

## Output Profiles

Three profiles are compiled in (all with report ID 0 to avoid Android quirks):

| Profile | Bytes | Contents |
|---------|-------|---------|
| `gamepad` | 9 | 16 buttons, LX/LY/RX/RY int8, LT/RT uint8, hat nibble |
| `mouse`   | 4 | 5 buttons, rel X/Y/wheel int8 |
| `joystick`| 13 | 32 buttons, 8 × int8 axes, hat |

Switch profile at runtime via CDC command (`profile gamepad`). The Pico
re-enumerates USB by briefly disconnecting (PIO-driven D+ pull-up toggled)
so the host sees the new descriptor.

## Repository Layout

```
├── CMakeLists.txt
├── build.ps1               PowerShell build helper (standalone)
├── uf2_gen.py              Pure-Python .bin → .uf2 converter (fallback)
├── btstack_config.h        BTstack: central + HID client + bonding
├── lwipopts.h              Minimal (CYW43 needs it even without WiFi)
├── tusb_config.h           TinyUSB: CDC + HID device
├── include/
│   ├── hid_parser.h        HID descriptor → field-map
│   ├── hid_merger.h        Semantic merge state
│   ├── output_profiles.h   USB HID descriptors + report structs
│   ├── ble_central.h       Multi-device BLE HoG central
│   ├── usb_output.h        TinyUSB HID output API
│   └── pairing_store.h     Flash-backed paired-device list
└── src/
    ├── main.c              Entry point, dual-core launch, CDC shell
    ├── ble_central.c       BTstack scan / connect / HIDS client
    ├── hid_parser.c        Stack-based HID descriptor parser
    ├── hid_merger.c        OR-merge + output-profile mapping
    ├── output_profiles.c   Report descriptors, report builders
    ├── usb_output.c        TinyUSB callbacks, report dispatch
    └── pairing_store.c     Flash TLV read/write for peer list
```

## CDC Shell Commands

Connect a serial terminal (115200 baud) to the Pico's CDC port:

```bash
help                                # Show command list
status                              # Connected devices + output profile
scan [on|off]                       # Start/stop BLE scan
pair <addr>                         # Bond to scanned device by address
unpair <addr>                       # Remove bond + stop connecting
list                                # Show paired device list
devices                             # List axes per slot
profile gamepad|mouse|joystick      # Switch output profile
axis_merge add|priority|last        # Set axis merge strategy
mouse_sens <1-32>                   # Mouse sensitivity divisor (higher = slower)
reset                               # Software reset
```

### Axis Output Binding

The `devices` command shows every axis and button that each connected device
exposes.  You can re-route any output role to a specific device+axis:

```bash
devices                     # list axes per slot
bind show                   # show current bindings (defaults shown as *)
bind lx 0 x                 # left stick X from slot-0's X axis
bind lt 1 z                 # left trigger from slot-1's Z axis
bind rx * rx                # right stick X = merged Rx across all devices
bind lx * relx              # left stick X from mouse delta-X (any slot)
bind lt default             # reset left trigger back to default (Z, any device)
bind reset                  # reset everything to defaults
```

Output roles: `lx  ly  rx  ry  lt  rt  hat  slider  dial  relx  rely  wheel`

Axis names:  `x  y  z  rx  ry  rz  slider  dial  wheel  hat  relx  rely  relwheel`

Roles not explicitly bound use the global `axis_merge` strategy across all
connected devices that report that axis.

---

## Building

Requires Pico SDK ≥ 2.0 with BTstack bundled.

**Use the Ninja generator** — the Pico SDK's boot stage 2 must be compiled
entirely by the ARM cross-toolchain, which the Visual Studio generator cannot
drive correctly on Windows.

```powershell
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=C:\path\to\pico-sdk
cmake --build build
```

Flash `build/mudra_hub.uf2` via BOOTSEL drag-and-drop.

### PowerShell build helper

```powershell
# Minimal — uses PICO_SDK_PATH / PICO_TOOLCHAIN_PATH environment variables:
.\build.ps1

# Explicit paths, GPIO buttons enabled, clean rebuild:
.\build.ps1 `
    -PicoSdkPath      C:\MyRepos\RPi\pico-sdk `
    -PicoToolchainPath C:\Toolchains\arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin `
    -GpioButtons -Clean

# Configure only, Debug build:
.\build.ps1 -BuildType Debug -ConfigureOnly
```

With GPIO buttons enabled:

```powershell
cmake -S . -B build -G Ninja -DHUB_GPIO_BUTTONS=ON -DPICO_SDK_PATH=C:\path\to\pico-sdk
cmake --build build
```

## Pairing Flow

1. `scan on` — hub advertises willingness to connect and scans for BLE HID
   devices.
2. When a controller appears in scan results it is printed to CDC.
3. `pair AA:BB:CC:DD:EE:FF` — hub initiates connection + Just Works pairing.
4. On success the address is written to flash; reconnection is automatic on
   next boot.
5. `unpair <addr>` removes the flash entry and drops the active connection.

Up to `MAX_PAIRED_DEVICES` (4) addresses are stored.

## Default Pinout

| Signal | Default GPIO | Direction | Notes |
|--------|-------------|-----------|-------|
| Button A (Pair) | GPIO 16 | Input | Pull-up; connect to GND |
| Button B (Mode) | GPIO 17 | Input | Pull-up; connect to GND |
| LED Pair status | GPIO 14 | Output | Active-high; use 220 Ω–1 kΩ resistor |
| LED Device count | GPIO 15 | Output | Active-high; use 220 Ω–1 kΩ resistor |

```
Pico 2W                         External

GPIO14 ──[220R]──|>|── GND      Pair status LED (active-high)
GPIO15 ──[220R]──|>|── GND      Device count LED (active-high)
GPIO16 ──────────────── GND     Button A (internal pull-up, active-low)
GPIO17 ──────────────── GND     Button B (internal pull-up, active-low)
```

### Button Gestures

| Button | Gesture | Action |
|--------|---------|--------|
| A (Pair) | Short press < 500 ms | Cycle selection through scan results |
| A (Pair) | Double-tap < 400 ms gap | Pair the selected scan result |
| A (Pair) | Hold ≥ 1 s | Toggle scan on / off |
| B (Mode) | Short press < 500 ms | Cycle output profile (Gamepad → Mouse → Joystick) |
| B (Mode) | Hold ≥ 1 s | Cycle axis merge strategy (Additive → Priority → Last) |

### LED Status

**Pair LED (GPIO 14)**

| State | Meaning |
|-------|---------|
| Off | Idle, not scanning |
| Fast blink 4 Hz | Actively scanning for BLE HID devices |
| Slow blink 1 Hz | Connecting / pairing in progress |
| Solid | At least one device connected |

**Device LED (GPIO 15)**

| State | Meaning |
|-------|---------|
| Off | No devices connected |
| N blinks, pause (1.2 s) | N devices currently connected |
| N blinks during scan | Scan result index N is selected for pairing |

When Button B short-press cycles the profile, the device LED momentarily
blinks 1 (gamepad), 2 (mouse), or 3 (joystick) times to confirm the change.
