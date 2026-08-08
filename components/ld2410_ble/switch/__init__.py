import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_SWITCH,
    ICON_BLUETOOTH,
    ENTITY_CATEGORY_CONFIG,
    ICON_PULSE,
)
from .. import CONF_LD2410_ID, LD2410BLEComponent, ld2410_ble_ns

BluetoothSwitch = ld2410_ble_ns.class_("BluetoothSwitch", switch.Switch)
EngineeringModeSwitch = ld2410_ble_ns.class_("EngineeringModeSwitch", switch.Switch)
CalibrateSwitch = ld2410_ble_ns.class_("CalibrateSwitch", switch.Switch)

CONF_ENGINEERING_MODE = "engineering_mode"
CONF_BLUETOOTH = "bluetooth"
CONF_CALIBRATE = "calibrate"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_LD2410_ID): cv.use_id(LD2410BLEComponent),
    cv.Optional(CONF_ENGINEERING_MODE): switch.switch_schema(
        EngineeringModeSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_PULSE,
    ),
    cv.Optional(CONF_BLUETOOTH): switch.switch_schema(
        BluetoothSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_BLUETOOTH,
    ),
    # Doesn't touch the sensor -- just gates whether handle_periodic_data_() publishes the
    # g0..g8 gate sensors to HA, so per-gate calibration data doesn't have to flood the event
    # bus once you're done tuning. Defaults on so existing configs see gate data as before.
    cv.Optional(CONF_CALIBRATE): switch.switch_schema(
        CalibrateSwitch,
        default_restore_mode="RESTORE_DEFAULT_ON",
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon="mdi:tune",
    ),
}


async def to_code(config):
    ld2410_component = await cg.get_variable(config[CONF_LD2410_ID])
    if engineering_mode_config := config.get(CONF_ENGINEERING_MODE):
        s = await switch.new_switch(engineering_mode_config)
        await cg.register_parented(s, config[CONF_LD2410_ID])
        cg.add(ld2410_component.set_engineering_mode_switch(s))
    if bluetooth_config := config.get(CONF_BLUETOOTH):
        s = await switch.new_switch(bluetooth_config)
        await cg.register_parented(s, config[CONF_LD2410_ID])
        cg.add(ld2410_component.set_bluetooth_switch(s))
    if calibrate_config := config.get(CONF_CALIBRATE):
        s = await switch.new_switch(calibrate_config)
        await cg.register_parented(s, config[CONF_LD2410_ID])
        cg.add(ld2410_component.set_calibrate_switch(s))
