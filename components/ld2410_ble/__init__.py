import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, uart, esp32_ble_tracker
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.core import CORE, ID
from esphome.const import (
    CONF_ID,
    CONF_THROTTLE,
    CONF_TIMEOUT,
    CONF_PASSWORD,
    CONF_TYPE,
    CONF_INTERNAL,
    CONF_NAME,
    CONF_DISABLED,
    CONF_DISABLED_BY_DEFAULT,
    CONF_UART_ID,
)


# The C++ class conditionally inherits ble_client::BLEClientNode/esp32_ble_tracker::
# ESPBTDeviceListener under USE_LD2410_BLE_CLIENT, and uart::UARTDevice under
# USE_LD2410_UART_ID -- both #define'd below in to_code() whenever the corresponding config
# key is actually present on *some* instance. This is what makes `ble_client_id`/`uart_id`
# genuinely independent optionals (see _validate_ld2410_ble): a device using only one
# transport doesn't pay for the other's BLE/UART machinery in its binary at all.
#
# uart needs a *dynamic* AUTO_LOAD (a callable, not a static list) for the same reason: it
# should only be pulled into the build for devices that actually have an instance using
# uart_id, not unconditionally for every ld2410_ble: device the way it used to be. uart's own
# MULTI_CONF_NO_DEFAULT lets it degrade to an empty uart: [] gracefully if this ever returns
# an empty list for every instance on a device -- unlike ble_client (see
# _validate_ld2410_ble's own comment on why ble_client is never auto-loaded, only ever
# user-declared).
def AUTO_LOAD(config):
    return ["uart"] if CONF_UART_ID in config else []
CODEOWNERS = ["@shammysha", "@sebcaps", "@regevbr"]
MULTI_CONF = True

ld2410_ble_ns = cg.esphome_ns.namespace("ld2410_ble")
LD2410BLEComponent = ld2410_ble_ns.class_(
    "LD2410BLEComponent",
    ble_client.BLEClientNode,
    uart.UARTDevice,
    esp32_ble_tracker.ESPBTDeviceListener,
    cg.Component,
)

CONF_LD2410_ID = "ld2410_id"
CONF_BLE_CLIENT_ID = "ble_client_id"
CONF_MAC_SUFFIX = "mac_suffix"

# `disabled: true` on ld2410_ble: is a *runtime* flag, not a YAML !remove-style component
# removal -- deliberately so. Removing uart:/ble_client:/ld2410_ble: down to zero real
# instances hits a genuine upstream ESPHome bug (ble_client's own headers reference stale
# symbols that only resolve once its to_code() has run for a real instance -- see the
# project's own notes/commit history for the full story). Since there's no YAML-level way to
# conditionally delete a top-level *key* (only list *items*), and no way to synthesize a real,
# schema-validated ble_client instance from Python either (would need to go through the
# normal config-validation pipeline, which only processes what's actually in the YAML tree),
# keeping uart:/ble_client:/ld2410_ble: always real (never empty) sidesteps the bug entirely.
# "Disabled" instead means: don't do any actual BLE/UART work (set_disabled() in C++, see
# ld2410_ble.h/.cpp), and hide every entity from Home Assistant regardless of what the
# template wrote for that entity's own `internal:` field (force_internal_if_disabled() below,
# used by every platform's to_code()).
#
# Stored under CORE.data, not a bare module-level set: the ESPHome Dashboard is a single
# long-running process that validates/compiles many devices without spawning a fresh
# subprocess per device (confirmed: no subprocess/Popen use in its own compile path) and
# without necessarily re-importing this module between them -- a plain module global would
# leak entries across unrelated devices' compiles (e.g. two devices both using `place: test`
# would produce the same id and wrongly share disabled-state). CORE.data is the documented,
# established place for exactly this kind of per-compile-run state (see e.g. the slot_counter
# helper in esphome/cpp_helpers.py); it's reset between runs, this dict is not.
_KEY_DISABLED_INSTANCES = "ld2410_ble_disabled_instances"


def _disabled_instances() -> set:
    return CORE.data.setdefault(_KEY_DISABLED_INSTANCES, set())


def force_internal_if_disabled(conf: dict, ld2410_id) -> dict:
    """Force internal: true on an entity's own validated sub-config when its parent
    ld2410_ble: instance has disabled: true -- regardless of what internal: the entity's own
    YAML says. Call this on each platform's per-entity config dict *before* passing it to
    that platform's new_XXX() (e.g. binary_sensor.new_binary_sensor()), since setup_entity()
    reads CONF_INTERNAL directly off that dict.
    """
    if ld2410_id in _disabled_instances():
        conf[CONF_INTERNAL] = True
    return conf

# The HiLink phone app identifies a module by only the last 2 bytes (4 hex digits) of its
# MAC address. "FF:63", "ff63", etc. are all accepted. "unknown" is the disabled sentinel,
# matching the tx/rx/mac_address 'unknown' convention already used by the packages templates
# in this repo -- it lets a template always include a `mac_suffix: ${mac_suffix}` line, with
# most instances simply never overriding the substitution away from its default.
MAC_SUFFIX_RE = re.compile(r"^([0-9A-Fa-f]{2}):?([0-9A-Fa-f]{2})$")
MAC_SUFFIX_DISABLED = "unknown"


def _validate_mac_suffix(value):
    value = cv.string_strict(value)
    if value.lower() == MAC_SUFFIX_DISABLED:
        return None
    match = MAC_SUFFIX_RE.match(value)
    if match is None:
        raise cv.Invalid(
            "mac_suffix must be the last 2 bytes of the MAC address, as shown in the "
            "HiLink app, e.g. 'FF:63' or 'FF63'"
        )
    return [int(match.group(1), 16), int(match.group(2), 16)]


def _validate_ld2410_ble(config):
    # At least one of ble_client_id/uart_id is required -- the C++ class only inherits
    # ble_client::BLEClientNode/esp32_ble_tracker::ESPBTDeviceListener under
    # USE_LD2410_BLE_CLIENT, and uart::UARTDevice under USE_LD2410_UART_ID (both #define'd in
    # to_code() below whenever the corresponding key is present on some instance) -- so an
    # instance with neither key would compile against a class with no transport at all. This
    # used to be a hard "ble_client_id always" requirement (before that conditional-compile
    # split existed, the class unconditionally inherited both bases, and ble_client couldn't be
    # auto-loaded with zero real instances the way uart can -- see AUTO_LOAD's own comment
    # above); a pure UART-only ld2410_ble: instance is now exactly as viable as a pure
    # BLE-only one, mirroring the native `ld2410` component's own shape when no BLE is wanted
    # at all.
    if CONF_BLE_CLIENT_ID not in config and CONF_UART_ID not in config:
        raise cv.Invalid(
            "ld2410_ble requires at least one of 'ble_client_id' or 'uart_id'"
        )
    if config.get(CONF_MAC_SUFFIX) is not None and CONF_BLE_CLIENT_ID not in config:
        raise cv.Invalid("mac_suffix requires 'ble_client_id' -- it only discovers/redirects a BLE connection")
    if config.get(CONF_MAC_SUFFIX) is not None:
        # Resolve esp32_ble_id (the same way esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA would)
        # only for instances that actually use mac_suffix -- adding this key unconditionally
        # (e.g. via .extend(ESP_BLE_DEVICE_SCHEMA) on the base schema) would make ESPHome's
        # final validation require an esp32_ble_tracker: to exist even for instances that
        # never use mac_suffix.
        config[esp32_ble_tracker.CONF_ESP32_BLE_ID] = ID(
            None, is_declaration=False, type=esp32_ble_tracker.ESP32BLETracker
        )
    if config.get(CONF_DISABLED):
        _disabled_instances().add(config[CONF_ID])
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LD2410BLEComponent),
            cv.Optional(CONF_PASSWORD, default="HiLink"): cv.sensitive(cv.string_strict),
            cv.Optional(CONF_BLE_CLIENT_ID): cv.use_id(ble_client.BLEClient),
            cv.Optional(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_MAC_SUFFIX, default=MAC_SUFFIX_DISABLED): _validate_mac_suffix,
            cv.Optional(CONF_DISABLED, default=False): cv.boolean,
            # Rate-limits how often a periodic data frame is actually processed (sensor
            # readings, binary_sensor states, the engineering_mode switch's own state) --
            # "Reduce data update rate to prevent home assistant database size grow fast" per
            # handle_periodic_data_()'s own comment. Default 0 = no throttling, every frame
            # processed. uint16_t on the C++ side, hence the 65535ms cap.
            cv.Optional(CONF_THROTTLE, default="0ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(milliseconds=65535)),
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_ld2410_ble,
)


def _final_validate(config):
    # Only enforce the UART bus shape (tx/rx pins, parity, stop bits) when uart_id was
    # actually configured for this instance -- BLE-only instances don't touch UART at all.
    if CONF_UART_ID in config:
        uart.final_validate_device_schema(
            "ld2410_ble", require_tx=True, require_rx=True, parity="NONE", stop_bits=1
        )(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_BLE_CLIENT_ID in config:
        # Compiles in ble_client::BLEClientNode/esp32_ble_tracker::ESPBTDeviceListener as base
        # classes -- see the class declaration's own #ifdef in ld2410_ble.h. Defining this once
        # (from whichever instance on the device happens to use ble_client_id first) is enough:
        # it's a device-wide compile flag, not a per-instance one.
        cg.add_define("USE_LD2410_BLE_CLIENT")
        await ble_client.register_ble_node(var, config)
    if CONF_UART_ID in config:
        # Compiles in uart::UARTDevice as a base class -- see AUTO_LOAD's own comment above for
        # why `uart:` itself also only gets pulled into the build under the same condition.
        cg.add_define("USE_LD2410_UART_ID")
        await uart.register_uart_device(var, config)
        cg.add(var.mark_uart_configured())
    if config[CONF_MAC_SUFFIX] is not None:
        await esp32_ble_tracker.register_ble_device(var, config)
        high_byte, low_byte = config[CONF_MAC_SUFFIX]
        cg.add(var.set_mac_suffix(high_byte, low_byte))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_disabled(config[CONF_DISABLED]))
    cg.add(var.set_throttle(config[CONF_THROTTLE].total_milliseconds))


CALIBRATION_ACTION_SCHEMA = maybe_simple_id(
    {
        cv.Required(CONF_ID): cv.use_id(LD2410BLEComponent),
    }
)


# Actions
BluetoothPasswordSetAction = ld2410_ble_ns.class_(
    "BluetoothPasswordSetAction", automation.Action
)


BLUETOOTH_PASSWORD_SET_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(LD2410BLEComponent),
        cv.Required(CONF_PASSWORD): cv.sensitive(cv.templatable(cv.string_strict)),
    }
)


@automation.register_action(
    "bluetooth_password.set",
    BluetoothPasswordSetAction,
    BLUETOOTH_PASSWORD_SET_SCHEMA,
    # BluetoothPasswordSetAction::play() (automation.h) is a plain synchronous override --
    # it calls set_bluetooth_password() directly and returns, no play_complex()/play_next_()
    # deferral to a callback, timer, or loop().
    synchronous=True,
)
async def bluetooth_password_set_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_PASSWORD], args, cg.std_string)
    # set_password(), not bluetooth_set_password() -- TEMPLATABLE_VALUE(std::string, password)
    # in automation.h auto-generates a set_password() setter for the templatable value; play()
    # then reads it back via .value(x...) and forwards it to the real
    # set_bluetooth_password() on the parent LD2410BLEComponent.
    cg.add(var.set_password(template_))
    return var
