# esphome-ld2410-ble

*[Читать по-русски](README.ru.md)*

An ESPHome external component for the HiLink LD2410 presence sensor that talks to the
sensor over its onboard **BLE radio**, its wired **UART** interface, or **both at once**
with automatic failover between them.

## Why

The stock ESPHome [`ld2410`](https://esphome.io/components/ld2410) component only
supports the wired UART interface. Some installs don't have a UART link available (or
want one as a backup only), but the LD2410 module also exposes the exact same
configuration/data protocol over its own BLE radio — this component talks that protocol
directly via `ble_client`, and can optionally also talk it over UART, using whichever
link is currently alive.

## Full native `ld2410` config compatibility

Every entity and config field from the stock ESPHome [`ld2410`](https://esphome.io/components/ld2410)
component is supported here under the exact same names: `has_target`/`has_moving_target`/
`has_still_target`/`out_pin_presence_status`, all the distance/energy sensors and per-gate
`g0`–`g8` groups, `engineering_mode`/`bluetooth` switches, `timeout`/gate threshold numbers,
`distance_resolution`/`light_function`/`out_pin_level`/`baud_rate` selects, the
`factory_reset`/`restart`/`query_params` buttons, and the `version`/`mac_address` text
sensors. An existing native `ld2410:` config's entity blocks carry over as-is — just point
them at `ld2410_ble:` instead and add the BLE-specific connection fields below.

## Features

- Presence/motion sensors, per-gate energy sensors, distance sensors, firmware/MAC text
  sensors, engineering-mode + Bluetooth switches, gate threshold/timeout numbers, distance
  resolution/light-control/baud-rate selects, factory reset/restart/query buttons — the
  same entity set as the native `ld2410` component (see above).
- **UART + BLE failover**: configure both `uart_id` and `ble_client_id` and the component
  keeps both links open. Sensor readings are published from whichever transport delivers
  a valid frame — so presence data stays continuous if either link drops. Outbound
  commands go out over UART whenever it has produced a valid frame in the last 2 seconds
  (or immediately fail over to BLE for 10s after a UART framing error), falling back to
  BLE otherwise. An `active_transport` diagnostic text sensor reports which one is
  currently preferred.
- BLE-only or UART-only also both work — just configure the one you have.
- Outbound writes (numbers/switches/selects) are ACK-tracked against the sensor's actual
  reply before being published, instead of assumed to have succeeded.
- Changing the `baud_rate` select live-reloads the UART peripheral at the new speed
  instead of just requiring a manual reflash.

`ld2410_ble:` also takes a few options with no equivalent in the native `ld2410:` schema —
plus `uart_id`, which exists in native too but behaves differently here. `ble_client_id` and
`uart_id` are each independently optional, but **at least one of the two is required**; each
one's own BLE/UART machinery is only compiled in when it's actually used, so a UART-only
instance carries no BLE/`esp32_ble_tracker` cost at all (smaller flash/RAM than either the
BLE-only or dual-transport configurations), and vice versa:

| Option | Required | Default | Logic |
| --- | --- | --- | --- |
| `ble_client_id` | At least one of `ble_client_id`/`uart_id` | — | The `ble_client:` instance to talk to the sensor's onboard BLE radio over. No equivalent in native `ld2410:`. |
| `uart_id` | At least one of `ble_client_id`/`uart_id` | — | The `uart:` bus wired to the sensor — same field native `ld2410:` has, but **required there, optional here**. |
| `mac_suffix` | No | `"unknown"` | Last 2 bytes of the module's BLE MAC (as shown in the HiLink app). When set, `ble_client:`'s own `mac_address:` is just a placeholder — the component finds the real device by BLE scan (matching the low 2 bytes of the advertised address) and redirects the `ble_client` to it. `"unknown"` disables discovery. |
| `password` | No | `"HiLink"` | The BLE password gate the sensor expects before accepting commands. |
| `disabled` | No | `false` | Runtime flag. `true` stops all BLE/UART activity (no connect/scan/read) and forces every entity to `internal: true` (hidden from Home Assistant) — the instance and its entities stay declared, nothing is removed. |

## Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/shammysha/esphome-ld2410-ble
      ref: main
    components: [ld2410_ble]
```

## Configuration example (dual transport)

```yaml
esp32_ble_tracker:

uart:
  - id: my_ld2410_uart
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 256000
    parity: NONE
    stop_bits: 1

ble_client:
  - mac_address: AA:BB:CC:DD:EE:FF
    id: my_ld2410_ble_client

ld2410_ble:
  - id: my_ld2410
    uart_id: my_ld2410_uart        # at least one of uart_id/ble_client_id is required
    ble_client_id: my_ld2410_ble_client  # (see the Features table above)
    password: "HiLink"             # BLE password gate, defaults to "HiLink"

binary_sensor:
  - platform: ld2410_ble
    ld2410_id: my_ld2410
    has_moving_target:
      name: Moving Target
    has_still_target:
      name: Still Target

text_sensor:
  - platform: ld2410_ble
    ld2410_id: my_ld2410
    active_transport:
      name: LD2410 Active Transport
```

See [`ld2410-ble-component-template.yaml`](ld2410-ble-component-template.yaml) for a BLE-only
reusable packages template, [`ld2410-uart-only-template.yaml`](ld2410-uart-only-template.yaml)
for the UART-only variant, and [`ld2410-uart-ble-template.yaml`](ld2410-uart-ble-template.yaml)
for the dual-transport one — all take `place`/`password`/`disabled` substitutions (plus
`mac_address`/`mac_suffix` for the two with BLE, `tx`/`rx` for the two with UART) and expose
the full entity set.
[`ld2410-ble-component-test.yaml`](ld2410-ble-component-test.yaml) /
[`ld2410-uart-only-test.yaml`](ld2410-uart-only-test.yaml) /
[`ld2410-uart-ble-test.yaml`](ld2410-uart-ble-test.yaml) are runnable example device configs
built on top of them.

## Credits

Protocol/entity layout ported from and modeled after ESPHome's own
[`ld2410`](https://github.com/esphome/esphome/tree/dev/esphome/components/ld2410)
component (Apache-2.0).
