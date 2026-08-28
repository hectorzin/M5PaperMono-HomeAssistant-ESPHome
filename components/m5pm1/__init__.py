# Local ESPHome component for the M5Stack M5PM1 PMIC (read-only battery monitoring).
# Register map derived from official M5Stack sources (MIT).
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
AUTO_LOAD = ["sensor", "binary_sensor"]
DEPENDENCIES = ["i2c"]

m5pm1_ns = cg.esphome_ns.namespace("m5pm1")
M5PM1Component = m5pm1_ns.class_("M5PM1Component", cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(M5PM1Component),
        }
    )
    .extend(cv.polling_component_schema("45s"))
    .extend(i2c.i2c_device_schema(0x6E))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
