"""ESPHome codegen for the gsl3680 touchscreen platform.

Port of guition-jc/esp_lcd_touch_gsl3680 (MIT, ESP-IDF 6.1 only) to the
ESPHome touchscreen and I2C APIs, so it builds on ESPHome's ESP-IDF 5.x.
The JC8012P4A1C controller firmware is compiled in (gsl3680_firmware.h).
"""

from esphome import pins
import esphome.codegen as cg
from esphome.components import i2c, touchscreen
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INTERRUPT_PIN,
    CONF_MIRROR_X,
    CONF_MIRROR_Y,
    CONF_RESET_PIN,
    CONF_SWAP_XY,
)

DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["touchscreen"]

gsl3680_ns = cg.esphome_ns.namespace("gsl3680")
GSL3680Touchscreen = gsl3680_ns.class_(
    "GSL3680Touchscreen",
    touchscreen.Touchscreen,
    i2c.I2CDevice,
)

# The driver reports panel-native coordinates (x on the 800 px side). The
# vendor configuration mirrors x; override with transform: if needed.
DEFAULTS = {
    CONF_SWAP_XY: False,
    CONF_MIRROR_X: True,
    CONF_MIRROR_Y: False,
}

CONFIG_SCHEMA = (
    touchscreen.touchscreen_schema(cv.UNDEFINED, False, DEFAULTS)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(GSL3680Touchscreen),
            cv.Optional(CONF_RESET_PIN, default=22): pins.gpio_output_pin_schema,
            # Optional: without it the chip is polled (update_interval), which
            # never misses a release.
            cv.Optional(CONF_INTERRUPT_PIN): pins.internal_gpio_input_pin_schema,
        }
    )
    .extend(i2c.i2c_device_schema(0x40))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await touchscreen.register_touchscreen(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))
    if CONF_INTERRUPT_PIN in config:
        cg.add(
            var.set_interrupt_pin(
                await cg.gpio_pin_expression(config[CONF_INTERRUPT_PIN])
            )
        )
