# ESPHome MIPI DSI fork upstream record

Upstream baseline: ESPHome `dev` commit
`5f6a910e2d6e41d3716668a66e5dff8cca25f2ea`, component path
`esphome/components/mipi_dsi`.

The local fork preserves the validated P4 panel models and initialization,
exposes the immutable framebuffer to the narrow video adapter, and serializes
display submissions so LVGL and direct video cannot consume each other's DMA
completion event. Optional video diagnostics remain compile-time gated.

Checked with:

```bash
git diff --no-index ../esphome-pr-work/esphome/components/mipi_dsi esphome/components/mipi_dsi
```

Re-run the comparison after every ESPHome update and keep unrelated upstream
model definitions intact.

## Guition copy

Copied from n-IA-hane/esphome-intercom `esphome/components/mipi_dsi` at commit
`c3cd0bbd8561b7443b30886a68f794236f435322` (MIT licence). One change:
`mipi_dsi.cpp` leaves `phy_clk_src` at 0 instead of
`MIPI_DSI_PHY_CLK_SRC_DEFAULT`. That constant is a pre-v3 clock source, so
`esp_lcd_new_dsi_bus()` aborts on ESP32-P4 v3.x silicon (chip revision v3.2 on
the JC8012P4A1). ESPHome core made the same change upstream.
