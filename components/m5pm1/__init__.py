# Local ESPHome component for the M5Stack M5PM1 PMIC.
# Register map derived from official M5Stack sources (MIT).
import esphome.codegen as cg
from esphome import pins
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
AUTO_LOAD = ["sensor", "binary_sensor"]
DEPENDENCIES = ["i2c"]

CONF_IRQ_PIN = "irq_pin"

m5pm1_ns = cg.esphome_ns.namespace("m5pm1")
M5PM1Component = m5pm1_ns.class_("M5PM1Component", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(M5PM1Component),
            cv.Required(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x6E))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    irq_pin = await cg.gpio_pin_expression(config[CONF_IRQ_PIN])
    cg.add(var.set_irq_pin(irq_pin))
