import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, select, sensor, switch, text_sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)
from esphome import pins

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["button", "select", "sensor", "switch", "text_sensor"]

CONF_DIRECTION_PIN = "direction_pin"
CONF_PUMP_A = "pump_a"
CONF_PUMP_B = "pump_b"
CONF_HEATER_PUMP = "heater_pump"
CONF_BLOWER_CYCLE = "blower_cycle"
CONF_BLOWER_STATE = "blower_state"
CONF_MENU_UP = "menu_up"
CONF_MENU_DOWN = "menu_down"
CONF_MENU_SELECT = "menu_select"
CONF_LIGHT_INTENSITY = "light_intensity"
CONF_LIGHT_MODE = "light_mode"
CONF_POOL_TEMP = "pool_temp"
CONF_SET_TEMP = "set_temp"
CONF_DISPLAY_LINE_1 = "display_line_1"
CONF_DISPLAY_LINE_2 = "display_line_2"
CONF_CMD_25 = "cmd_25"
CONF_CMD_28 = "cmd_28"
CONF_CMD_31 = "cmd_31"
CONF_CMD_32 = "cmd_32"
CONF_CONTROLLER_TIME = "controller_time"

pool_controller_ns = cg.esphome_ns.namespace("pool_controller")
PoolController = pool_controller_ns.class_("PoolController", cg.Component)
PoolSwitch = pool_controller_ns.class_("PoolSwitch", switch.Switch)
HeaterPumpSelect = pool_controller_ns.class_("HeaterPumpSelect", select.Select)
BlowerCycleButton = pool_controller_ns.class_("BlowerCycleButton", button.Button)
KeyButton = pool_controller_ns.class_("KeyButton", button.Button)

# conf key -> (payload byte 0, 1, 2, hold_ms) per the SP1200 command-80 map
KEY_BUTTONS = {
    CONF_MENU_UP: (0x01, 0x00, 0x01, 200),
    CONF_MENU_DOWN: (0x04, 0x00, 0x01, 200),
    CONF_MENU_SELECT: (0x02, 0x00, 0x01, 200),
    CONF_LIGHT_INTENSITY: (0x00, 0x01, 0x01, 300),
    CONF_LIGHT_MODE: (0x40, 0x00, 0x01, 300),
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PoolController),
            cv.Optional(CONF_DIRECTION_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_PUMP_A): switch.switch_schema(PoolSwitch),
            cv.Required(CONF_PUMP_B): switch.switch_schema(PoolSwitch),
            cv.Required(CONF_HEATER_PUMP): select.select_schema(HeaterPumpSelect),
            cv.Required(CONF_BLOWER_CYCLE): button.button_schema(BlowerCycleButton),
            cv.Required(CONF_BLOWER_STATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_POOL_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_SET_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_DISPLAY_LINE_1): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_DISPLAY_LINE_2): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CMD_25): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CMD_28): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CMD_31): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CMD_32): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CONTROLLER_TIME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_MENU_UP): button.button_schema(KeyButton),
            cv.Optional(CONF_MENU_DOWN): button.button_schema(KeyButton),
            cv.Optional(CONF_MENU_SELECT): button.button_schema(KeyButton),
            cv.Optional(CONF_LIGHT_INTENSITY): button.button_schema(KeyButton),
            cv.Optional(CONF_LIGHT_MODE): button.button_schema(KeyButton),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    controller = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(controller, config)
    await uart.register_uart_device(controller, config)

    if CONF_DIRECTION_PIN in config:
        direction_pin = await cg.gpio_pin_expression(config[CONF_DIRECTION_PIN])
        cg.add(controller.set_direction_pin(direction_pin))

    pump_a_config = config[CONF_PUMP_A]
    pump_a = cg.new_Pvariable(pump_a_config[CONF_ID], controller, 0)
    await switch.register_switch(pump_a, pump_a_config)
    cg.add(controller.set_pump_a(pump_a))

    pump_b_config = config[CONF_PUMP_B]
    pump_b = cg.new_Pvariable(pump_b_config[CONF_ID], controller, 1)
    await switch.register_switch(pump_b, pump_b_config)
    cg.add(controller.set_pump_b(pump_b))

    heater_config = config[CONF_HEATER_PUMP]
    heater = cg.new_Pvariable(heater_config[CONF_ID], controller)
    await select.register_select(
        heater, heater_config, options=["OFF", "AUTO", "LOW"]
    )
    cg.add(controller.set_heater_pump(heater))

    blower_config = config[CONF_BLOWER_CYCLE]
    blower = cg.new_Pvariable(blower_config[CONF_ID], controller)
    await button.register_button(blower, blower_config)

    blower_state = await text_sensor.new_text_sensor(config[CONF_BLOWER_STATE])
    cg.add(controller.set_blower_state(blower_state))

    if CONF_POOL_TEMP in config:
        pool_temp = await sensor.new_sensor(config[CONF_POOL_TEMP])
        cg.add(controller.set_pool_temp(pool_temp))

    if CONF_SET_TEMP in config:
        set_temp = await sensor.new_sensor(config[CONF_SET_TEMP])
        cg.add(controller.set_set_temp(set_temp))

    if CONF_DISPLAY_LINE_1 in config:
        display_line_1 = await text_sensor.new_text_sensor(config[CONF_DISPLAY_LINE_1])
        cg.add(controller.set_display_line_1(display_line_1))

    if CONF_DISPLAY_LINE_2 in config:
        display_line_2 = await text_sensor.new_text_sensor(config[CONF_DISPLAY_LINE_2])
        cg.add(controller.set_display_line_2(display_line_2))

    for conf_key, setter in (
        (CONF_CMD_25, controller.set_cmd_25),
        (CONF_CMD_28, controller.set_cmd_28),
        (CONF_CMD_31, controller.set_cmd_31),
        (CONF_CMD_32, controller.set_cmd_32),
    ):
        if conf_key in config:
            diag = await text_sensor.new_text_sensor(config[conf_key])
            cg.add(setter(diag))

    if CONF_CONTROLLER_TIME in config:
        controller_time = await text_sensor.new_text_sensor(config[CONF_CONTROLLER_TIME])
        cg.add(controller.set_controller_time(controller_time))

    for conf_key, (p0, p1, p2, hold_ms) in KEY_BUTTONS.items():
        if conf_key in config:
            key_config = config[conf_key]
            key = cg.new_Pvariable(key_config[CONF_ID], controller, p0, p1, p2, hold_ms)
            await button.register_button(key, key_config)
