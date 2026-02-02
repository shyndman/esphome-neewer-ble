import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, light
from esphome.components.rgbct import light as rgbct_light
from esphome.components.neewerlight import output as nw_output
from esphome.components.light import effects as light_effects
from esphome.const import (
    CONF_COLOR_INTERLOCK,
    CONF_EFFECTS,
    CONF_GAMMA_CORRECT,
    CONF_NAME,
    CONF_OUTPUT_ID,
)

CONF_GREEN_MAGENTA_BIAS = "green_magenta_bias"

CONF_MODEL = "model"
MODEL_RGB62 = "rgb62"

neewerlight_ns = cg.esphome_ns.namespace("neewerlight")

NeewerSceneLightEffect = neewerlight_ns.class_(
    "NeewerSceneLightEffect", light.LightEffect
)


@light_effects.register_rgb_effect(
    "neewer_scene",
    NeewerSceneLightEffect,
    "Neewer FX",
    {cv.Required("scene_id"): cv.int_range(min=1, max=9)},
)
async def neewer_scene_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(effect_id, config[CONF_NAME], config["scene_id"])
    return var


RGB62_SCENE_PRESETS = [
    (1, "Neewer FX • Lighting"),
    (2, "Neewer FX • Paparazzi"),
    (3, "Neewer FX • Defective Bulb"),
    (4, "Neewer FX • Explosion"),
    (5, "Neewer FX • Welding"),
    (6, "Neewer FX • CCT Flash"),
    (7, "Neewer FX • Hue Flash"),
    (8, "Neewer FX • CCT Pulse"),
    (9, "Neewer FX • Hue Pulse"),
]


def _ensure_scene_effects(config):
    effects = config.setdefault(CONF_EFFECTS, [])

    def has_scene(scene_id):
        for entry in effects:
            if (
                "neewer_scene" in entry
                and entry["neewer_scene"].get("scene_id") == scene_id
            ):
                return True
        return False

    for scene_id, name in RGB62_SCENE_PRESETS:
        if has_scene(scene_id):
            continue
        effects.append({"neewer_scene": {CONF_NAME: name, "scene_id": scene_id}})


DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["output", "rgbct"]
IS_PLATFORM_COMPONENT = True

rgbct_ns = cg.esphome_ns.namespace("rgbct")

NeewerRGBCTLightOutput = neewerlight_ns.class_(
    "NeewerRGBCTLightOutput",
    rgbct_light.RGBCTLightOutput,
    nw_output.NeewerBLEOutput,
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(NeewerRGBCTLightOutput),
            cv.Required(CONF_NAME): cv.string,
            cv.Required(ble_client.CONF_BLE_CLIENT_ID): cv.use_id(ble_client.BLEClient),
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            cv.Optional(CONF_COLOR_INTERLOCK, default=True): cv.boolean,
            cv.Required(CONF_MODEL): cv.one_of(MODEL_RGB62, lower=True),
            cv.Optional(CONF_GREEN_MAGENTA_BIAS, default=0.0): cv.float_range(
                min=-50.0, max=50.0
            ),
        }
    )
    .extend(cv.ENTITY_BASE_SCHEMA)
    .extend(light.RGB_LIGHT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    if config[CONF_MODEL] == MODEL_RGB62:
        _ensure_scene_effects(config)

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)

    await ble_client.register_ble_node(var, config)

    cg.add(var.set_color_interlock(config[CONF_COLOR_INTERLOCK]))
    cg.add(var.set_kelvin_range(3200.0, 5600.0))
    cg.add(var.set_supports_green_magenta(False))
    cg.add(var.set_green_magenta_bias(config[CONF_GREEN_MAGENTA_BIAS]))
    if config[CONF_MODEL] == MODEL_RGB62:
        cg.add(var.set_kelvin_range(2500.0, 8500.0))
        cg.add(var.set_supports_green_magenta(True))
