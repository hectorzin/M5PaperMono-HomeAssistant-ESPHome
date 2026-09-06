import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "m5ioe1", "papermono_activity", "controls"]

CONF_M5IOE1_ID = "m5ioe1_id"
CONF_ACTIVITY_ID = "activity_id"
CONF_IRQ_PIN = "irq_pin"
CONF_UID_SENSOR = "uid_sensor"
CONF_CONTROLS_ID = "controls_id"

papermono_nfc_ns = cg.esphome_ns.namespace("papermono_nfc")
PaperMonoNfc = papermono_nfc_ns.class_("PaperMonoNfc", cg.Component, i2c.I2CDevice)
M5IOE1Component = cg.esphome_ns.namespace("m5ioe1").class_("M5IOE1Component")
PaperMonoActivityComponent = cg.esphome_ns.namespace("papermono_activity").class_("PaperMonoActivityComponent")
Controls = cg.esphome_ns.namespace("controls").class_("Controls")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PaperMonoNfc),
        cv.Required(CONF_M5IOE1_ID): cv.use_id(M5IOE1Component),
        cv.Required(CONF_ACTIVITY_ID): cv.use_id(PaperMonoActivityComponent),
        cv.Required(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_UID_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_CONTROLS_ID): cv.use_id(Controls),
    }
).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x50))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_m5ioe1(await cg.get_variable(config[CONF_M5IOE1_ID])))
    cg.add(var.set_activity(await cg.get_variable(config[CONF_ACTIVITY_ID])))
    cg.add(var.set_uid_sensor(await cg.get_variable(config[CONF_UID_SENSOR])))
    cg.add(var.set_controls(await cg.get_variable(config[CONF_CONTROLS_ID])))
    cg.add(var.set_irq_pin(await cg.gpio_pin_expression(config[CONF_IRQ_PIN])))
    cg.add((await cg.get_variable(config[CONF_ACTIVITY_ID])).set_nfc(var))
