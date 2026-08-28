import esphome.codegen as cg
from esphome.components import i2c, m5pm1, papermono_activity
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["i2c", "m5pm1", "papermono_activity"]

CONF_M5PM1_ID = "m5pm1_id"
CONF_ACTIVITY_ID = "activity_id"

papermono_imu_ns = cg.esphome_ns.namespace("papermono_imu")
PaperMonoImuComponent = papermono_imu_ns.class_("PaperMonoImuComponent", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PaperMonoImuComponent),
            cv.Required(CONF_M5PM1_ID): cv.use_id(m5pm1.M5PM1Component),
            cv.Required(CONF_ACTIVITY_ID): cv.use_id(papermono_activity.PaperMonoActivityComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x68))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    pmu = await cg.get_variable(config[CONF_M5PM1_ID])
    activity = await cg.get_variable(config[CONF_ACTIVITY_ID])
    cg.add(var.set_m5pm1(pmu))
    cg.add(var.set_activity(activity))
