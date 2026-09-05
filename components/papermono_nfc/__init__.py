import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "m5ioe1", "controls", "papermono_activity"]
CONF_M5IOE1_ID = "m5ioe1_id"
CONF_IRQ_PIN = "irq_pin"

papermono_nfc_ns = cg.esphome_ns.namespace("papermono_nfc")
PaperMonoNfc = papermono_nfc_ns.class_("PaperMonoNfc", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PaperMonoNfc),
    cv.Required(CONF_M5IOE1_ID): cv.use_id(cg.esphome_ns.namespace("m5ioe1").class_("M5IOE1Component")),
    cv.Required("controls_id"): cv.use_id(cg.esphome_ns.namespace("controls").class_("Controls")),
    cv.Required("activity_id"): cv.use_id(cg.esphome_ns.namespace("papermono_activity").class_("PaperMonoActivityComponent")),
    cv.Optional(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
}).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x50))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_m5ioe1(await cg.get_variable(config[CONF_M5IOE1_ID])))
    cg.add(var.set_controls(await cg.get_variable(config["controls_id"])))
    cg.add(var.set_activity(await cg.get_variable(config["activity_id"])))
    if CONF_IRQ_PIN in config:
        irq = await cg.gpio_pin_expression(config[CONF_IRQ_PIN])
        cg.add(var.set_irq_pin(irq))
