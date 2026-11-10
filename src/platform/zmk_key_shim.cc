#include "key.h"
#include "key_code.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern "C" int zmk_javelin_emit_keycode(uint16_t usage_page, uint32_t keycode, bool pressed);

static constexpr uint16_t HID_USAGE_PAGE_KEYBOARD = 0x07;
static constexpr uint16_t HID_USAGE_PAGE_CONSUMER = 0x0C;

static void emit_keycode(KeyCode key, bool pressed) {
  uint32_t v = key.value;
  if (v == 0) {
    return;
  }
  uint16_t usage_page;
  uint32_t keycode;

  if (v >= 0x10000) {
    usage_page = HID_USAGE_PAGE_CONSUMER;
    keycode = v & 0xFFFF;
  } else {
    usage_page = HID_USAGE_PAGE_KEYBOARD;
    keycode = v;
  }

  const int rc = zmk_javelin_emit_keycode(usage_page, keycode, pressed);
  LOG_DBG("[JAV] HID %x:%x %s rc=%d", usage_page, keycode, pressed ? "down" : "up", rc);
}

bool Key::historyEnabled = false;

void Key::Press(KeyCode key) { emit_keycode(key, true); }

void Key::Release(KeyCode key) { emit_keycode(key, false); }

void Key::Flush() {}
