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

## Features

- Presence/motion sensors, per-gate energy sensors, distance sensors, firmware/MAC text
  sensors, engineering-mode + Bluetooth switches, gate threshold/timeout numbers, distance
  resolution/light-control selects, factory reset/restart/query buttons — the same entity
  set as the native `ld2410` component.
- **UART + BLE failover**: configure both `uart_id` and `ble_client_id` and the component
  keeps both links open. Sensor readings are published from whichever transport delivers
  a valid frame — so presence data stays continuous if either link drops. Outbound
  commands go out over UART whenever it has produced a valid frame in the last 2 seconds,
  falling back to BLE otherwise. An `active_transport` diagnostic text sensor reports
  which one is currently preferred.
- BLE-only or UART-only also both work — just configure the one you have.
- Optional `mac_suffix` discovery: give just the last 2 bytes of the module's MAC (as
  shown in the HiLink app) and the component finds and connects to it via BLE scanning.
- Outbound writes (numbers/switches/selects) are ACK-tracked against the sensor's actual
  reply before being published, instead of assumed to have succeeded.

## Installation

```yaml
external_components:
  - source:
      type: local
      path: components  # or a github source, see ESPHome docs on external_components
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
    uart_id: my_ld2410_uart        # optional
    ble_client_id: my_ld2410_ble_client  # optional — at least one of the two is required
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

See [`ld2410-ble-component-template.yaml`](ld2410-ble-component-template.yaml) for a BLE-only reusable
packages template, and [`ld2410-uart-ble-template.yaml`](ld2410-uart-ble-template.yaml)
for the dual-transport variant — both take `place`/`mac_address`/`mac_suffix`/`password`/
`disabled` substitutions (and `tx`/`rx` for the dual-transport one) and expose the full
entity set. `disabled: true` fully removes the instance (no leftover component or entities)
rather than just hiding it.
[`ld2410-ble-component-test.yaml`](ld2410-ble-component-test.yaml) / [`ld2410-uart-ble-test.yaml`](ld2410-uart-ble-test.yaml)
are runnable example device configs built on top of them.

## Status

Compiles cleanly under both the `esp-idf` and `arduino` ESP32 frameworks. Both the
BLE-only and the dual-transport (UART+BLE) variants have now been flashed to real
hardware; the failover behavior itself (e.g. pulling the UART wire to confirm a smooth
handover to BLE) hasn't been exercised on real hardware yet.

## Credits

Protocol/entity layout ported from and modeled after ESPHome's own
[`ld2410`](https://github.com/esphome/esphome/tree/dev/esphome/components/ld2410)
component (Apache-2.0).
