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
#define PORTRAIT_STRIDE DIV_ROUND_UP(PORTRAIT_WIDTH, 8)
#define I1_PALETTE_BYTES (2 * sizeof(lv_color32_t))

BUILD_ASSERT(PANEL_WIDTH == 160 && PANEL_HEIGHT == 68);
BUILD_ASSERT(LV_DRAW_BUF_STRIDE_ALIGN == 1);

static uint8_t draw_buffer[PORTRAIT_STRIDE * PORTRAIT_HEIGHT + I1_PALETTE_BYTES]
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

    /* Full refresh supplies every pixel. Match the old Practice canvas's
     * 270-degree rotation and pack the panel's LSB-first rows with 1 for white.
     */
    pixels += I1_PALETTE_BYTES;
    for (unsigned x = 0; x < PORTRAIT_WIDTH; x++) {
        for (unsigned byte = 0; byte < PANEL_STRIDE; byte++) {
            uint8_t packed = 0;
            for (unsigned bit = 0; bit < 8; bit++) {
                unsigned y = PORTRAIT_HEIGHT - 1 - (byte * 8 + bit);
                unsigned pixel = (pixels[y * PORTRAIT_STRIDE + x / 8] >> (7 - x % 8)) & 1;
                packed |= pixel << bit;
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
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_FULL);
}
