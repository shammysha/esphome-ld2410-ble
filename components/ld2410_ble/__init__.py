import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, uart, esp32_ble_tracker
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.core import ID
from esphome.const import (
    CONF_ID,
    CONF_THROTTLE,
    CONF_TIMEOUT,
    CONF_PASSWORD,
    CONF_TYPE,
    CONF_INTERNAL,
    CONF_NAME,
    CONF_DISABLED_BY_DEFAULT,
    CONF_UART_ID,
)



# The component unconditionally inherits uart::UARTDevice AND ble_client::BLEClientNode in
# C++ (to support UART/BLE failover), so both need their headers/sources present in the build
# regardless of which one a given instance actually wires up.
#
# AUTO_LOAD is a *dynamic* callable here (ESPHome's AddDynamicAutoLoadsValidationStep,
# config.py, priority -5.0) rather than a static list. Confirmed via that step's source:
#   conf = result.get_nested_item(self.path)
#   ...
#   loads = auto_load(conf)
# it's invoked exactly once per domain, with `conf` being the full (already schema-validated)
# list of ld2410_ble: instances in this config -- not once per instance -- and only when the
# ld2410_ble: key is present in config at all. With zero instances (key absent entirely) it
# never fires, so uart/ble_client are never auto-loaded from here.
#
# NOTE this alone does NOT fix `disabled: true` support -- that turned out to need a change
# on the packages/YAML side instead (see ld2410-uart-ble-inactive.yaml), because the actual
# bug is triggered by ble_client's own top-level key being *present* in the merged config
# (even as `ble_client: []`, e.g. after a template's `!remove` empties its list) -- ESPHome
# bundles a domain's C++ sources whenever its key exists at all, regardless of item count or
# of what any *other* component's AUTO_LOAD says. Reproduced minimally with nothing but a
# bare `ble_client: []` in an unrelated config: ble_client.h/automation.h reference stale
# symbol names (espbt::ESPBTDevice, esp32_ble::UUID_STR_LEN) that only resolve once
# ble_client's own to_code() has actually run for a real instance, which never happens with
# zero items. This function is kept anyway as a smaller, genuinely-correct improvement in its
# own right (a static list would auto-load uart/ble_client even for a bare `ld2410_ble: []`
# with no real instances, which is never useful).

CONF_LD2410_ID = "ld2410_id"
CONF_BLE_CLIENT_ID = "ble_client_id"
CONF_MAC_SUFFIX = "mac_suffix"
CONF_DISABLED = "disabled"

def _ld2410_ble_auto_load(config):
    if CONF_DISABLED in config and config[CONF_DISABLED] == True:
        return []
    return ["uart", "ble_client"]


AUTO_LOAD = _ld2410_ble_auto_load
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
    # ble_client_id is unconditionally required (not just "at least one of ble_client_id/
    # uart_id"): the C++ class unconditionally inherits ble_client::BLEClientNode, and unlike
    # uart, the ble_client component can't be AUTO_LOAD-ed with zero configured instances (it
    # doesn't declare MULTI_CONF_NO_DEFAULT, so validation fails asking for a mac_address
    # instead of degrading to an empty list). A pure UART-only device is better served by the
    # native `ld2410` component anyway, which doesn't carry any BLE/esp32_ble_tracker cost.
    if CONF_BLE_CLIENT_ID not in config:
        raise cv.Invalid(
            "ld2410_ble requires 'ble_client_id' (uart_id may additionally be configured for "
            "UART/BLE failover, but BLE is not optional for this component -- for a "
            "UART-only LD2410, use the native 'ld2410' component instead)"
        )
    if config.get(CONF_MAC_SUFFIX) is not None:
        # Resolve esp32_ble_id (the same way esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA would)
        # only for instances that actually use mac_suffix -- adding this key unconditionally
        # (e.g. via .extend(ESP_BLE_DEVICE_SCHEMA) on the base schema) would make ESPHome's
        # final validation require an esp32_ble_tracker: to exist even for instances that
        # never use mac_suffix.
        config[esp32_ble_tracker.CONF_ESP32_BLE_ID] = ID(
            None, is_declaration=False, type=esp32_ble_tracker.ESP32BLETracker
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LD2410BLEComponent),
            cv.Optional(CONF_PASSWORD, default="HiLink"): cv.sensitive(cv.string_strict),
            cv.Optional(CONF_BLE_CLIENT_ID): cv.use_id(ble_client.BLEClient),
            cv.Optional(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_MAC_SUFFIX, default=MAC_SUFFIX_DISABLED): _validate_mac_suffix,
            cv.Optional(CONF_DISABLED, default=True): cv.boolean,
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
        await ble_client.register_ble_node(var, config)
    if CONF_UART_ID in config:
        await uart.register_uart_device(var, config)
        cg.add(var.mark_uart_configured())
    if config[CONF_MAC_SUFFIX] is not None:
        await esp32_ble_tracker.register_ble_device(var, config)
        high_byte, low_byte = config[CONF_MAC_SUFFIX]
        cg.add(var.set_mac_suffix(high_byte, low_byte))
    cg.add(var.set_password(config[CONF_PASSWORD]))


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
    cg.add(var.bluetooth_set_password(template_))
    return var
