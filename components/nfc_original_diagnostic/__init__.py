import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "m5ioe1"]
nfc_diag_ns = cg.esphome_ns.namespace("nfc_original_diagnostic")
NfcOriginalDiagnostic = nfc_diag_ns.class_("NfcOriginalDiagnostic", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(NfcOriginalDiagnostic),
    cv.Required("m5ioe1_id"): cv.use_id(cg.esphome_ns.namespace("m5ioe1").class_("M5IOE1Component")),
}).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x50))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_m5ioe1(await cg.get_variable(config["m5ioe1_id"])))
