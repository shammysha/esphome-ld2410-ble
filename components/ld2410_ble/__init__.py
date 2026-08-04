import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, uart
from esphome import automation
from esphome.automation import maybe_simple_id
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


# The component unconditionally inherits uart::UARTDevice in C++ (to support UART/BLE
# failover) even when a given instance only configures ble_client_id -- AUTO_LOAD makes sure
# uart's headers/sources are always present in the build, not just when uart_id is used.
AUTO_LOAD = ["uart"]
CODEOWNERS = ["@shammysha", "@sebcaps", "@regevbr"]
MULTI_CONF = True

ld2410_ble_ns = cg.esphome_ns.namespace("ld2410_ble")
LD2410BLEComponent = ld2410_ble_ns.class_(
    "LD2410BLEComponent", ble_client.BLEClientNode, uart.UARTDevice, cg.Component
)

CONF_LD2410_ID = "ld2410_id"
CONF_BLE_CLIENT_ID = "ble_client_id"


def _require_at_least_one_transport(config):
    if CONF_BLE_CLIENT_ID not in config and CONF_UART_ID not in config:
        raise cv.Invalid(
            "ld2410_ble requires at least one of 'ble_client_id' or 'uart_id' "
            "(configure both for UART/BLE failover)"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LD2410BLEComponent),
            cv.Optional(CONF_PASSWORD, default="HiLink"): cv.string_strict,
            cv.Optional(CONF_BLE_CLIENT_ID): cv.use_id(ble_client.BLEClient),
            cv.Optional(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _require_at_least_one_transport,
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
        cv.Required(CONF_PASSWORD): cv.templatable(cv.string_strict),
    }
)


@automation.register_action(
    "bluetooth_password.set", BluetoothPasswordSetAction, BLUETOOTH_PASSWORD_SET_SCHEMA
)
async def bluetooth_password_set_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_PASSWORD], args, cg.std_string)
    cg.add(var.bluetooth_set_password(template_))
    return var
