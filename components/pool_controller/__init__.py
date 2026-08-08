import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, select, switch, text_sensor, uart
from esphome.const import CONF_ID
from esphome import pins

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["button", "select", "switch", "text_sensor"]

CONF_DIRECTION_PIN = "direction_pin"
CONF_PUMP_A = "pump_a"
CONF_PUMP_B = "pump_b"
CONF_HEATER_PUMP = "heater_pump"
CONF_BLOWER_CYCLE = "blower_cycle"
CONF_BLOWER_STATE = "blower_state"

pool_controller_ns = cg.esphome_ns.namespace("pool_controller")
PoolController = pool_controller_ns.class_("PoolController", cg.Component)
PoolSwitch = pool_controller_ns.class_("PoolSwitch", switch.Switch)
HeaterPumpSelect = pool_controller_ns.class_("HeaterPumpSelect", select.Select)
BlowerCycleButton = pool_controller_ns.class_("BlowerCycleButton", button.Button)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PoolController),
            cv.Required(CONF_DIRECTION_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_PUMP_A): switch.switch_schema(PoolSwitch),
            cv.Required(CONF_PUMP_B): switch.switch_schema(PoolSwitch),
            cv.Required(CONF_HEATER_PUMP): select.select_schema(HeaterPumpSelect),
            cv.Required(CONF_BLOWER_CYCLE): button.button_schema(BlowerCycleButton),
            cv.Required(CONF_BLOWER_STATE): text_sensor.text_sensor_schema(),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    controller = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(controller, config)
    await uart.register_uart_device(controller, config)

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
