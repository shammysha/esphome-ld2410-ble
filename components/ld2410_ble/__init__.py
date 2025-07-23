import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID, CONF_THROTTLE, CONF_TIMEOUT, CONF_PASSWORD
from esphome import automation
from esphome.automation import maybe_simple_id

DEPENDENCIES = ["ble_client", "sensor"]
CODEOWNERS = ["@shammysha", "@sebcaps", "@regevbr"]
MULTI_CONF = True

ld2410_ble_ns = cg.esphome_ns.namespace("ld2410_ble")
LD2410BLEComponent = ld2410_ble_ns.class_("LD2410BLEComponent", cg.PollingComponent, ble_client.BLEClientNode)

CONF_LD2410_ID = "ld2410_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LD2410BLEComponent),
        cv.Optional(CONF_PASSWORD, default="HiLink"): cv.string_strict
    }
).extend(cv.polling_component_schema("30sec")).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)    
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
