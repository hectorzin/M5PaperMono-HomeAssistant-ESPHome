import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals, sensor, text_sensor
from esphome.components.papermono_epaper import display as papermono_display
from esphome.components.homeassistant import sensor as ha_sensor
from esphome.components.homeassistant import text_sensor as ha_text_sensor
from esphome.const import CONF_ENTITY_ID, CONF_ID, CONF_NAME

DEPENDENCIES = ["api", "sensor", "text_sensor"]

CONF_CONTROLS = "controls"
MAX_CONTROLS = 6
SUPPORTED_DOMAINS = {"climate", "light", "cover", "vacuum", "switch", "media_player"}

CONF_STATE_SENSOR_ID = "state_sensor_id"
CONF_FRIENDLY_NAME_SENSOR_ID = "friendly_name_sensor_id"
CONF_MODES_SENSOR_ID = "modes_sensor_id"
CONF_TARGET_TEMPERATURE_SENSOR_ID = "target_temperature_sensor_id"
CONF_CURRENT_TEMPERATURE_SENSOR_ID = "current_temperature_sensor_id"
CONF_MIN_TEMPERATURE_SENSOR_ID = "min_temperature_sensor_id"
CONF_MAX_TEMPERATURE_SENSOR_ID = "max_temperature_sensor_id"
CONF_BRIGHTNESS_SENSOR_ID = "brightness_sensor_id"
CONF_CURRENT_POSITION_SENSOR_ID = "current_position_sensor_id"
CONF_VOLUME_SENSOR_ID = "volume_sensor_id"
CONF_MEDIA_TITLE_SENSOR_ID = "media_title_sensor_id"
CONF_SUPPORTED_FEATURES_SENSOR_ID = "supported_features_sensor_id"
CONF_MEDIA_ARTIST_SENSOR_ID = "media_artist_sensor_id"
CONF_MEDIA_ALBUM_NAME_SENSOR_ID = "media_album_name_sensor_id"

controls_ns = cg.esphome_ns.namespace("controls")
Controls = controls_ns.class_("Controls", cg.Component)

CONTROL_SCHEMA = cv.Schema({
    cv.Required(CONF_ENTITY_ID): cv.entity_id,
    cv.Optional(CONF_NAME, default=""): cv.string,
})


def _declare_control_sensor_ids(controls):
    controls = cv.All(cv.ensure_list(CONTROL_SCHEMA), cv.Length(max=MAX_CONTROLS))(controls)
    declared = []
    for index, control in enumerate(controls):
        entity = control[CONF_ENTITY_ID]
        domain = entity.split(".", 1)[0]
        supported = domain in SUPPORTED_DOMAINS
        domain = domain if supported else "none"
        prefix = f"controls_{index + 1}"

        entry = dict(control)
        entry[CONF_STATE_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(
            f"{prefix}_state"
        )
        entry[CONF_FRIENDLY_NAME_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(
            f"{prefix}_friendly_name"
        )

        if domain == "climate":
            entry[CONF_MODES_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(
                f"{prefix}_hvac_modes"
            )
            entry[CONF_TARGET_TEMPERATURE_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_target_temperature"
            )
            entry[CONF_CURRENT_TEMPERATURE_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_current_temperature"
            )
            entry[CONF_MIN_TEMPERATURE_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_min_temperature"
            )
            entry[CONF_MAX_TEMPERATURE_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_max_temperature"
            )
        elif domain == "light":
            entry[CONF_MODES_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(
                f"{prefix}_supported_color_modes"
            )
            entry[CONF_BRIGHTNESS_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_brightness"
            )
            entry["hs_color_sensor_id"] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(
                f"{prefix}_hs_color"
            )
        elif domain == "cover":
            entry[CONF_CURRENT_POSITION_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(
                f"{prefix}_current_position"
            )
        elif domain == "media_player":
            entry[CONF_VOLUME_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(f"{prefix}_volume_level")
            entry[CONF_MEDIA_TITLE_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(f"{prefix}_media_title")
            entry[CONF_SUPPORTED_FEATURES_SENSOR_ID] = cv.declare_id(ha_sensor.HomeassistantSensor)(f"{prefix}_supported_features")
            entry[CONF_MEDIA_ARTIST_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(f"{prefix}_media_artist")
            entry[CONF_MEDIA_ALBUM_NAME_SENSOR_ID] = cv.declare_id(ha_text_sensor.HomeassistantTextSensor)(f"{prefix}_media_album_name")

        declared.append(entry)
    return declared


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Controls),
    cv.Required(CONF_CONTROLS): _declare_control_sensor_ids,
    cv.Required("display_id"): cv.use_id(papermono_display.PaperMonoEpaper),
    cv.Required("controls_view"): cv.use_id(globals.GlobalsComponent),
    cv.Required("ha_connection_state"): cv.use_id(globals.GlobalsComponent),
}).extend(cv.COMPONENT_SCHEMA)


def _text_config(identifier, entity, attribute=None):
    config = {CONF_ID: identifier, "internal": True, "entity_id": entity}
    if attribute is not None:
        config["attribute"] = attribute
    return ha_text_sensor.CONFIG_SCHEMA(config)


def _number_config(identifier, entity, attribute):
    return ha_sensor.CONFIG_SCHEMA({
        CONF_ID: identifier,
        "internal": True,
        "entity_id": entity,
        "attribute": attribute,
        "accuracy_decimals": 1,
    })


async def _make_text(identifier, entity, attribute=None):
    await ha_text_sensor.to_code(_text_config(identifier, entity, attribute))
    return await cg.get_variable(identifier)


async def _make_number(identifier, entity, attribute):
    await ha_sensor.to_code(_number_config(identifier, entity, attribute))
    return await cg.get_variable(identifier)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(await cg.get_variable(config["display_id"])))
    cg.add(var.set_controls_view(await cg.get_variable(config["controls_view"])))
    cg.add(var.set_ha_connection_state(await cg.get_variable(config["ha_connection_state"])))

    for index, control in enumerate(config[CONF_CONTROLS]):
        entity = control[CONF_ENTITY_ID]
        domain = entity.split(".", 1)[0]
        supported = domain in SUPPORTED_DOMAINS
        domain = domain if supported else "none"
        print(f"[controls] codegen slot {index} {entity} domain={domain}")

        state = await _make_text(control[CONF_STATE_SENSOR_ID], entity)
        friendly = await _make_text(control[CONF_FRIENDLY_NAME_SENSOR_ID], entity, "friendly_name")
        modes = brightness = hs_color = current = minimum = maximum = target = current_position = cg.nullptr
        volume = media_title = supported_features = media_artist = media_album_name = cg.nullptr

        print(f"[controls]   state\n[controls]   friendly_name")
        if domain == "climate":
            modes = await _make_text(control[CONF_MODES_SENSOR_ID], entity, "hvac_modes")
            target = await _make_number(control[CONF_TARGET_TEMPERATURE_SENSOR_ID], entity, "temperature")
            current = await _make_number(control[CONF_CURRENT_TEMPERATURE_SENSOR_ID], entity, "current_temperature")
            minimum = await _make_number(control[CONF_MIN_TEMPERATURE_SENSOR_ID], entity, "min_temp")
            maximum = await _make_number(control[CONF_MAX_TEMPERATURE_SENSOR_ID], entity, "max_temp")
            print("[controls]   hvac_modes\n[controls]   temperature\n[controls]   current_temperature\n[controls]   min_temp\n[controls]   max_temp")
        elif domain == "light":
            modes = await _make_text(control[CONF_MODES_SENSOR_ID], entity, "supported_color_modes")
            brightness = await _make_number(control[CONF_BRIGHTNESS_SENSOR_ID], entity, "brightness")
            hs_color = await _make_text(control["hs_color_sensor_id"], entity, "hs_color")
            print("[controls]   supported_color_modes\n[controls]   brightness\n[controls]   hs_color")
        elif domain == "cover":
            current_position = await _make_number(control[CONF_CURRENT_POSITION_SENSOR_ID], entity, "current_position")
            print("[controls]   current_position")
        elif domain == "media_player":
            volume = await _make_number(control[CONF_VOLUME_SENSOR_ID], entity, "volume_level")
            media_title = await _make_text(control[CONF_MEDIA_TITLE_SENSOR_ID], entity, "media_title")
            supported_features = await _make_number(control[CONF_SUPPORTED_FEATURES_SENSOR_ID], entity, "supported_features")
            media_artist = await _make_text(control[CONF_MEDIA_ARTIST_SENSOR_ID], entity, "media_artist")
            media_album_name = await _make_text(control[CONF_MEDIA_ALBUM_NAME_SENSOR_ID], entity, "media_album_name")
            print("[controls]   volume_level\n[controls]   media_title\n[controls]   supported_features\n[controls]   media_artist\n[controls]   media_album_name")
        else:
            print(f"[controls]   no domain-specific attributes for {domain}")

        cg.add(var.add_control(index, entity, control[CONF_NAME], domain, state, friendly, modes,
                               hs_color,
                               brightness, current, minimum, maximum, target, current_position,
                               volume, media_title, supported_features, media_artist, media_album_name))
