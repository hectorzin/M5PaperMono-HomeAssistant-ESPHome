import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "m5ioe1"]

nfc_ns = cg.esphome_ns.namespace("nfc_official_diagnostic")
NfcOfficialDiagnostic = nfc_ns.class_("NfcOfficialDiagnostic", cg.Component, i2c.I2CDevice)

CONF_M5IOE1_ID = "m5ioe1_id"
CONF_IRQ_PIN = "irq_pin"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NfcOfficialDiagnostic),
        cv.Required(CONF_M5IOE1_ID): cv.use_id(
            cg.esphome_ns.namespace("m5ioe1").class_("M5IOE1Component")
        ),
        cv.Required(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x50))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_m5ioe1(await cg.get_variable(config[CONF_M5IOE1_ID])))
    cg.add(var.set_irq_pin(await cg.gpio_pin_expression(config[CONF_IRQ_PIN])))
