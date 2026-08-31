import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID

from . import PaperMonoFrontlightOutput

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(PaperMonoFrontlightOutput)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await output.register_output(var, config)
