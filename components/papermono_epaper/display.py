from esphome import pins
import esphome.codegen as cg
from esphome.components import display, m5pm1, spi
from esphome.components.display import validate_rotation
import esphome.config_validation as cv
from esphome.const import (
    CONF_BUSY_PIN,
    CONF_DC_PIN,
    CONF_DIMENSIONS,
    CONF_FULL_UPDATE_EVERY,
    CONF_HEIGHT,
    CONF_ID,
    CONF_LAMBDA,
    CONF_MIRROR_X,
    CONF_MIRROR_Y,
    CONF_RESET_PIN,
    CONF_ROTATION,
    CONF_TRANSFORM,
    CONF_WIDTH,
)

DEPENDENCIES = ["spi"]

CONF_M5PM1_ID = "m5pm1_id"

papermono_epaper_ns = cg.esphome_ns.namespace("papermono_epaper")
PaperMonoEpaper = papermono_epaper_ns.class_(
    "PaperMonoEpaper", cg.PollingComponent, spi.SPIDevice, display.Display
)

NATIVE_WIDTH = 800
NATIVE_HEIGHT = 480

TRANSFORM_FLAG_MIRROR_X = 1
TRANSFORM_FLAG_MIRROR_Y = 2


def validate_dimensions(value):
    value = cv.Schema(
        {
            cv.Required(CONF_WIDTH): cv.int_,
            cv.Required(CONF_HEIGHT): cv.int_,
        }
    )(value)
    if value[CONF_WIDTH] != NATIVE_WIDTH or value[CONF_HEIGHT] != NATIVE_HEIGHT:
        raise cv.Invalid(
            f"papermono_epaper is fixed at {NATIVE_WIDTH}x{NATIVE_HEIGHT} (SSD1677 native)"
        )
    return value


CONFIG_SCHEMA = (
    display.FULL_DISPLAY_SCHEMA.extend(
        spi.spi_device_schema(
            cs_pin_required=True,
            default_mode="MODE0",
            default_data_rate="20MHz",
        )
    )
    .extend(
        {
            cv.GenerateID(): cv.declare_id(PaperMonoEpaper),
            cv.Optional(CONF_ROTATION, default=0): validate_rotation,
            cv.Optional(CONF_FULL_UPDATE_EVERY, default=0): cv.int_range(min=0, max=255),
            cv.Optional(CONF_DIMENSIONS): validate_dimensions,
            cv.Optional(CONF_TRANSFORM): cv.Schema(
                {
                    cv.Required(CONF_MIRROR_X): cv.boolean,
                    cv.Required(CONF_MIRROR_Y): cv.boolean,
                }
            ),
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_M5PM1_ID): cv.use_id(m5pm1.M5PM1Component),
        }
    )
)


FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "papermono_epaper", require_miso=False, require_mosi=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config, write_only=True)

    dc = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc))
    reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset))
    busy = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(busy))
    cg.add(var.set_full_update_every(config[CONF_FULL_UPDATE_EVERY]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))

    transform = config.get(CONF_TRANSFORM, {})
    flags = 0
    if transform.get(CONF_MIRROR_X):
        flags |= TRANSFORM_FLAG_MIRROR_X
    if transform.get(CONF_MIRROR_Y):
        flags |= TRANSFORM_FLAG_MIRROR_Y
    cg.add(var.set_transform(flags))

    if CONF_M5PM1_ID in config:
        pmu = await cg.get_variable(config[CONF_M5PM1_ID])
        cg.add(var.set_pmu(pmu))
