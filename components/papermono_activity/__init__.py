import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals, m5pm1
from esphome.components.papermono_epaper import display as papermono_epaper_display
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["m5pm1", "globals"]

CONF_M5PM1_ID = "m5pm1_id"
CONF_DISPLAY_ID = "display_id"
CONF_CONTROLS_VIEW = "controls_view"
CONF_TIMEOUT = "timeout"
CONF_ON_BRIGHTNESS = "on_brightness"

papermono_activity_ns = cg.esphome_ns.namespace("papermono_activity")
PaperMonoActivityComponent = papermono_activity_ns.class_("PaperMonoActivityComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PaperMonoActivityComponent),
        cv.Required(CONF_M5PM1_ID): cv.use_id(m5pm1.M5PM1Component),
        cv.Required(CONF_DISPLAY_ID): cv.use_id(papermono_epaper_display.PaperMonoEpaper),
        cv.Required(CONF_CONTROLS_VIEW): cv.use_id(globals.GlobalsComponent),
        cv.Optional(CONF_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_BRIGHTNESS, default=30): cv.int_range(min=1, max=100),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    pmu = await cg.get_variable(config[CONF_M5PM1_ID])
    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    controls_view = await cg.get_variable(config[CONF_CONTROLS_VIEW])
    cg.add(var.set_m5pm1(pmu))
    cg.add(var.set_display(display))
    cg.add(var.set_controls_view(controls_view))
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT]))
    cg.add(var.set_on_brightness_percent(config[CONF_ON_BRIGHTNESS]))
