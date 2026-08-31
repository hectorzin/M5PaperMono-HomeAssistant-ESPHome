import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals, sensor, text_sensor
from esphome.components.papermono_epaper import display as papermono_display
from esphome.components.homeassistant import sensor as ha_sensor
from esphome.components.homeassistant import text_sensor as ha_text_sensor
from esphome.const import CONF_ENTITY_ID, CONF_ID, CONF_NAME
from esphome.core import ID
from esphome.core import CORE

CODEOWNERS = []
DEPENDENCIES = ["api", "sensor", "text_sensor"]
CONF_CONTROLS = "controls"
MAX_CONTROLS = 6
SUPPORTED_DOMAINS = {"climate", "light", "cover", "vacuum", "switch"}

controls_ns = cg.esphome_ns.namespace("controls")
Controls = controls_ns.class_("Controls", cg.Component)

CONTROL_SCHEMA = cv.Schema({
    cv.Required(CONF_ENTITY_ID): cv.entity_id,
    cv.Optional(CONF_NAME): cv.string,
})


def _validate_controls(value):
    value = cv.All(cv.ensure_list(CONTROL_SCHEMA), cv.Length(max=MAX_CONTROLS))(value)
    return value


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Controls),
    cv.Required(CONF_CONTROLS): _validate_controls,
    cv.Required("display_id"): cv.use_id(papermono_display.PaperMonoEpaper),
    cv.Required("controls_view"): cv.use_id(globals.GlobalsComponent),
    cv.Required("ha_connection_state"): cv.use_id(globals.GlobalsComponent),
}).extend(cv.COMPONENT_SCHEMA)


def _text_config(identifier, entity, attribute=None, on_value=False):
    config = {
        CONF_ID: identifier,
        "internal": True,
        "entity_id": entity,
    }
    if attribute is not None:
        config["attribute"] = attribute
    return ha_text_sensor.CONFIG_SCHEMA(config)


def _sensor_config(identifier, entity, attribute, on_value=False):
    config = {
        CONF_ID: identifier,
        "internal": True,
        "entity_id": entity,
        "attribute": attribute,
        "accuracy_decimals": 1,
    }
    return ha_sensor.CONFIG_SCHEMA(config)


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
        normalized_domain = domain if supported else "none"
        print(f"[controls] codegen slot {index}: entity={entity} domain={normalized_domain}")
        if not supported:
            cg.add(var.log_unsupported(index, entity))

        prefix = f"controls_{index + 1}"
        state_id = ID(f"{prefix}_state", is_declaration=True, type=ha_text_sensor.HomeassistantTextSensor)
        friendly_id = ID(f"{prefix}_friendly_name", is_declaration=True, type=ha_text_sensor.HomeassistantTextSensor)
        CORE.component_ids.update({str(state_id), str(friendly_id)})
        await ha_text_sensor.to_code(_text_config(state_id, entity, None, True))
        await ha_text_sensor.to_code(_text_config(friendly_id, entity, "friendly_name", True))
        print(f"[controls] codegen slot {index}: create state, friendly_name")

        modes_id = brightness_id = current_id = min_id = max_id = target_id = None
        if normalized_domain == "climate":
            modes_id = ID(f"{prefix}_hvac_modes", is_declaration=True, type=ha_text_sensor.HomeassistantTextSensor)
            CORE.component_ids.add(str(modes_id))
            await ha_text_sensor.to_code(_text_config(modes_id, entity, "hvac_modes", True))
            print(f"[controls] codegen slot {index}: create hvac_modes")
            target_id = ID(f"{prefix}_target_temperature", is_declaration=True, type=ha_sensor.HomeassistantSensor)
            current_id = ID(f"{prefix}_current_temperature", is_declaration=True, type=ha_sensor.HomeassistantSensor)
            min_id = ID(f"{prefix}_min_temperature", is_declaration=True, type=ha_sensor.HomeassistantSensor)
            max_id = ID(f"{prefix}_max_temperature", is_declaration=True, type=ha_sensor.HomeassistantSensor)
            for sid, attribute in ((target_id, "temperature"), (current_id, "current_temperature"),
                                   (min_id, "min_temp"), (max_id, "max_temp")):
                CORE.component_ids.add(str(sid))
                await ha_sensor.to_code(_sensor_config(sid, entity, attribute, True))
                print(f"[controls] codegen slot {index}: create {attribute}")
        elif normalized_domain == "light":
            modes_id = ID(f"{prefix}_supported_color_modes", is_declaration=True, type=ha_text_sensor.HomeassistantTextSensor)
            CORE.component_ids.add(str(modes_id))
            await ha_text_sensor.to_code(_text_config(modes_id, entity, "supported_color_modes", True))
            brightness_id = ID(f"{prefix}_brightness", is_declaration=True, type=ha_sensor.HomeassistantSensor)
            CORE.component_ids.add(str(brightness_id))
            await ha_sensor.to_code(_sensor_config(brightness_id, entity, "brightness", True))
            print(f"[controls] codegen slot {index}: create supported_color_modes, brightness")

        state = await cg.get_variable(state_id)
        friendly = await cg.get_variable(friendly_id)
        modes = await cg.get_variable(modes_id) if modes_id is not None else cg.nullptr
        brightness = await cg.get_variable(brightness_id) if brightness_id is not None else cg.nullptr
        current = await cg.get_variable(current_id) if current_id is not None else cg.nullptr
        minimum = await cg.get_variable(min_id) if min_id is not None else cg.nullptr
        maximum = await cg.get_variable(max_id) if max_id is not None else cg.nullptr
        target = await cg.get_variable(target_id) if target_id is not None else cg.nullptr
        cg.add(var.add_control(index, entity, control.get(CONF_NAME, ""), normalized_domain,
                               state, friendly, modes, brightness,
                               current, minimum, maximum, target))
