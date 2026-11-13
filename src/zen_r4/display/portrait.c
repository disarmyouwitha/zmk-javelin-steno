#include "portrait.h"

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PANEL DT_CHOSEN(zephyr_display)
#define PANEL_WIDTH DT_PROP(PANEL, width)
#define PANEL_HEIGHT DT_PROP(PANEL, height)
#define PORTRAIT_WIDTH PANEL_HEIGHT
#define PORTRAIT_HEIGHT PANEL_WIDTH
#define PANEL_STRIDE (PANEL_WIDTH / 8)

BUILD_ASSERT(PANEL_WIDTH == 160 && PANEL_HEIGHT == 68);
BUILD_ASSERT(LV_DRAW_BUF_STRIDE_ALIGN == 1);

static uint8_t draw_buffer[PORTRAIT_WIDTH * PORTRAIT_HEIGHT]
    __aligned(LV_DRAW_BUF_ALIGN);
static uint8_t panel_buffer[PANEL_STRIDE * PANEL_HEIGHT];

static void portrait_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const struct device *panel = DEVICE_DT_GET(PANEL);
    const struct display_buffer_descriptor descriptor = {
        .buf_size = sizeof(panel_buffer),
        .width = PANEL_WIDTH,
        .pitch = PANEL_WIDTH,
        .height = PANEL_HEIGHT,
    };

    /* Quantize after LVGL blends the glyphs in grayscale. The direct I1
     * blender loses intermediate alpha levels while drawing the glyphs.
     * Match the tested rotation and pack LSB-first with 1 for white.
     */
    for (unsigned x = 0; x < PORTRAIT_WIDTH; x++) {
        for (unsigned byte = 0; byte < PANEL_STRIDE; byte++) {
            uint8_t packed = 0;
            for (unsigned bit = 0; bit < 8; bit++) {
                unsigned y = PORTRAIT_HEIGHT - 1 - (byte * 8 + bit);
                packed |= (pixels[y * PORTRAIT_WIDTH + x] >= 128) << bit;
            }
            panel_buffer[x * PANEL_STRIDE + byte] = packed;
        }
    }
    int err = display_write(panel, 0, 0, &descriptor, panel_buffer);
    if (err) {
        LOG_ERR("nice!view portrait write failed: %d", err);
    }
    lv_display_flush_ready(display);
}

void zen_r4_portrait_init(void) {
    lv_display_t *display = lv_display_get_default();
    lv_display_set_flush_cb(display, portrait_flush);
    /* Use full refresh for the portrait screen. This bypasses Zephyr's landscape
     * invalidation rounder and gives the adapter complete physical rows.
     */
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_L8);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_FULL);
}
