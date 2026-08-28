import esphome.codegen as cg
from esphome.components import binary_sensor, m5pm1
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["m5pm1"]

CONF_M5PM1_ID = "m5pm1_id"
CONF_EXTERNAL_POWER = "external_power"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_M5PM1_ID): cv.use_id(m5pm1.M5PM1Component),
        cv.Optional(CONF_EXTERNAL_POWER): binary_sensor.binary_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_M5PM1_ID])
    if CONF_EXTERNAL_POWER in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_EXTERNAL_POWER])
        cg.add(hub.set_external_power_binary_sensor(sens))
