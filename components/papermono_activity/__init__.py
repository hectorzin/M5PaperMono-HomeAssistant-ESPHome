import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, globals, light, m5pm1, output, papermono_rtc, script, sensor, switch, text_sensor, time
from esphome.components.papermono_epaper import display as papermono_epaper_display
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["m5pm1", "papermono_rtc", "globals", "time", "wifi", "api"]

CONF_M5PM1_ID = "m5pm1_id"
CONF_RTC_ID = "rtc_id"
CONF_DISPLAY_ID = "display_id"
CONF_CONTROLS_VIEW = "controls_view"
CONF_PENDING_CONTROLS_ENTRY = "pending_controls_entry"
CONF_CONTROLS_RETURN_HOME_PENDING = "controls_return_home_pending"
CONF_CONTROLS_PAGE = "controls_page"
CONF_TIMEOUT = "timeout"
CONF_ON_BRIGHTNESS = "on_brightness"
CONF_TIME_ID = "time_id"
CONF_SCREENSAVER_REFRESH_MINUTES = "screensaver_refresh_minutes"
CONF_QUIET_HOURS_START = "quiet_hours_start"
CONF_QUIET_HOURS_END = "quiet_hours_end"
CONF_HA_CONNECTION_STATE = "ha_connection_state"
CONF_HA_WEATHER_STATE = "ha_weather_state"
CONF_HA_INDOOR_TEMPERATURE = "ha_indoor_temperature"
CONF_HA_INDOOR_HUMIDITY = "ha_indoor_humidity"
CONF_WIFI_TRANSITION_PENDING = "wifi_transition_pending"
CONF_LIGHT_SLEEP_WAKE_RECOVERY = "light_sleep_wake_recovery"
CONF_STATUS_LED_SLEEP_PENDING = "status_led_sleep_pending"
CONF_STATUS_LED_PREVIEW_SLOT = "status_led_preview_slot"
CONF_STATUS_LED_BLUE_SWITCH = "status_led_blue_switch"
CONF_QUIET_HOURS_SLEEP_DISPLAY = "quiet_hours_sleep_display"
CONF_QUIET_HOURS_USER_OVERRIDE = "quiet_hours_user_override"
CONF_BATTERY_DISPLAY_LEVEL = "battery_display_level"
CONF_EXTERNAL_POWER = "external_power"
CONF_PERIODIC_ALERT_PULSE = "periodic_alert_pulse"
CONF_LOW_BATTERY_THRESHOLD = "low_battery_threshold"
CONF_FRONTLIGHT_DEFAULT_BRIGHTNESS = "frontlight_default_brightness"
CONF_FRONTLIGHT_TIMEOUT_SECONDS = "frontlight_timeout_seconds"
CONF_SLEEP_TIMEOUT_SECONDS = "sleep_timeout_seconds"
CONF_FRONTLIGHT_LIGHT = "frontlight_light"
CONF_FRONTLIGHT_OUTPUT = "frontlight_output"
CONF_NFC_ID = "nfc_id"

papermono_activity_ns = cg.esphome_ns.namespace("papermono_activity")
PaperMonoActivityComponent = papermono_activity_ns.class_("PaperMonoActivityComponent", cg.Component)
PaperMonoFrontlightOutput = papermono_activity_ns.class_("PaperMonoFrontlightOutput", output.FloatOutput)


def validate_screensaver_refresh_minutes(value):
    value = cv.positive_int(value)
    if 60 % value != 0:
        raise cv.Invalid("screensaver_refresh_minutes must divide 60 evenly (e.g. 5, 10, 15, 30)")
    return value


def validate_quiet_hours_time(value):
    value = cv.string(value)
    if len(value) != 5 or value[2] != ":":
        raise cv.Invalid("quiet hours time must be HH:MM")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PaperMonoActivityComponent),
        cv.Required(CONF_M5PM1_ID): cv.use_id(m5pm1.M5PM1Component),
        cv.Required(CONF_RTC_ID): cv.use_id(papermono_rtc.PaperMonoRtcComponent),
        cv.Required(CONF_DISPLAY_ID): cv.use_id(papermono_epaper_display.PaperMonoEpaper),
        cv.Required(CONF_CONTROLS_VIEW): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_PENDING_CONTROLS_ENTRY): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_CONTROLS_RETURN_HOME_PENDING): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_CONTROLS_PAGE): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_HA_CONNECTION_STATE): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_HA_WEATHER_STATE): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_HA_INDOOR_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HA_INDOOR_HUMIDITY): cv.use_id(sensor.Sensor),
        cv.Required(CONF_WIFI_TRANSITION_PENDING): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_LIGHT_SLEEP_WAKE_RECOVERY): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_STATUS_LED_SLEEP_PENDING): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_STATUS_LED_PREVIEW_SLOT): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_STATUS_LED_BLUE_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_QUIET_HOURS_SLEEP_DISPLAY): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_QUIET_HOURS_USER_OVERRIDE): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_BATTERY_DISPLAY_LEVEL): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_EXTERNAL_POWER): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_PERIODIC_ALERT_PULSE): cv.use_id(script.Script),
        cv.Required(CONF_LOW_BATTERY_THRESHOLD): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_FRONTLIGHT_DEFAULT_BRIGHTNESS): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_FRONTLIGHT_TIMEOUT_SECONDS): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_SLEEP_TIMEOUT_SECONDS): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_FRONTLIGHT_LIGHT): cv.use_id(light.LightState),
        cv.Required(CONF_FRONTLIGHT_OUTPUT): cv.use_id(PaperMonoFrontlightOutput),
        cv.Required(CONF_SCREENSAVER_REFRESH_MINUTES): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_QUIET_HOURS_START): cv.use_id(globals.GlobalsComponent),
        cv.Required(CONF_QUIET_HOURS_END): cv.use_id(globals.GlobalsComponent),
        cv.Optional(CONF_NFC_ID): cv.use_id(cg.esphome_ns.namespace("papermono_nfc").class_("PaperMonoNfc")),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    pmu = await cg.get_variable(config[CONF_M5PM1_ID])
    rtc = await cg.get_variable(config[CONF_RTC_ID])
    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    controls_view = await cg.get_variable(config[CONF_CONTROLS_VIEW])
    pending_controls_entry = await cg.get_variable(config[CONF_PENDING_CONTROLS_ENTRY])
    controls_return_home_pending = await cg.get_variable(config[CONF_CONTROLS_RETURN_HOME_PENDING])
    controls_page = await cg.get_variable(config[CONF_CONTROLS_PAGE])
    ha_time = await cg.get_variable(config[CONF_TIME_ID])
    ha_connection_state = await cg.get_variable(config[CONF_HA_CONNECTION_STATE])
    ha_weather_state = await cg.get_variable(config[CONF_HA_WEATHER_STATE])
    ha_indoor_temperature = await cg.get_variable(config[CONF_HA_INDOOR_TEMPERATURE])
    ha_indoor_humidity = await cg.get_variable(config[CONF_HA_INDOOR_HUMIDITY])
    wifi_transition_pending = await cg.get_variable(config[CONF_WIFI_TRANSITION_PENDING])
    light_sleep_wake_recovery = await cg.get_variable(config[CONF_LIGHT_SLEEP_WAKE_RECOVERY])
    status_led_sleep_pending = await cg.get_variable(config[CONF_STATUS_LED_SLEEP_PENDING])
    status_led_preview_slot = await cg.get_variable(config[CONF_STATUS_LED_PREVIEW_SLOT])
    status_led_blue_switch = await cg.get_variable(config[CONF_STATUS_LED_BLUE_SWITCH])
    quiet_hours_sleep_display = await cg.get_variable(config[CONF_QUIET_HOURS_SLEEP_DISPLAY])
    quiet_hours_user_override = await cg.get_variable(config[CONF_QUIET_HOURS_USER_OVERRIDE])
    battery_display_level = await cg.get_variable(config[CONF_BATTERY_DISPLAY_LEVEL])
    external_power = await cg.get_variable(config[CONF_EXTERNAL_POWER])
    periodic_alert_pulse = await cg.get_variable(config[CONF_PERIODIC_ALERT_PULSE])
    low_battery_threshold = await cg.get_variable(config[CONF_LOW_BATTERY_THRESHOLD])
    frontlight_default_brightness = await cg.get_variable(config[CONF_FRONTLIGHT_DEFAULT_BRIGHTNESS])
    frontlight_timeout_seconds = await cg.get_variable(config[CONF_FRONTLIGHT_TIMEOUT_SECONDS])
    sleep_timeout_seconds = await cg.get_variable(config[CONF_SLEEP_TIMEOUT_SECONDS])
    frontlight_light = await cg.get_variable(config[CONF_FRONTLIGHT_LIGHT])
    frontlight_output = await cg.get_variable(config[CONF_FRONTLIGHT_OUTPUT])
    screensaver_refresh_minutes = await cg.get_variable(config[CONF_SCREENSAVER_REFRESH_MINUTES])
    quiet_hours_start = await cg.get_variable(config[CONF_QUIET_HOURS_START])
    quiet_hours_end = await cg.get_variable(config[CONF_QUIET_HOURS_END])
    cg.add(var.set_m5pm1(pmu))
    cg.add(var.set_rtc(rtc))
    cg.add(var.set_display(display))
    cg.add(var.set_controls_view(controls_view))
    cg.add(var.set_pending_controls_entry(pending_controls_entry))
    cg.add(var.set_controls_return_home_pending(controls_return_home_pending))
    cg.add(var.set_controls_page(controls_page))
    cg.add(var.set_time(ha_time))
    cg.add(var.set_ha_connection_state(ha_connection_state))
    cg.add(var.set_ha_weather_state(ha_weather_state))
    cg.add(var.set_ha_indoor_temperature(ha_indoor_temperature))
    cg.add(var.set_ha_indoor_humidity(ha_indoor_humidity))
    cg.add(var.set_wifi_transition_pending(wifi_transition_pending))
    cg.add(var.set_light_sleep_wake_recovery(light_sleep_wake_recovery))
    cg.add(var.set_status_led_sleep_pending(status_led_sleep_pending))
    cg.add(var.set_status_led_preview_slot(status_led_preview_slot))
    cg.add(var.set_status_led_blue_switch(status_led_blue_switch))
    cg.add(var.set_quiet_hours_sleep_display(quiet_hours_sleep_display))
    cg.add(var.set_quiet_hours_user_override(quiet_hours_user_override))
    cg.add(var.set_battery_display_level(battery_display_level))
    cg.add(var.set_external_power(external_power))
    cg.add(var.set_periodic_alert_pulse(periodic_alert_pulse))
    cg.add(var.set_low_battery_threshold(low_battery_threshold))
    cg.add(var.set_frontlight_default_brightness(frontlight_default_brightness))
    cg.add(var.set_frontlight_timeout_seconds(frontlight_timeout_seconds))
    cg.add(var.set_sleep_timeout_seconds(sleep_timeout_seconds))
    cg.add(var.set_frontlight_light(frontlight_light))
    cg.add(var.set_frontlight_output(frontlight_output))
    cg.add(var.set_screensaver_refresh_minutes(screensaver_refresh_minutes))
    cg.add(var.set_quiet_hours_start(quiet_hours_start))
    cg.add(var.set_quiet_hours_end(quiet_hours_end))
    if CONF_NFC_ID in config:
        cg.add(var.set_nfc(await cg.get_variable(config[CONF_NFC_ID])))
