#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#define MATRIX_NODE DT_NODELABEL(left_matrix)
#define COLS DT_PROP_LEN(MATRIX_NODE, col_gpios)
#define KEY_COUNT 21

struct key_event {
	uint8_t row;
	uint8_t col;
	bool pressed;
};

K_MSGQ_DEFINE(key_events, sizeof(struct key_event), 32, 1);

static void matrix_event(struct input_event *event, void *user_data)
{
	static struct key_event key;

	ARG_UNUSED(user_data);
	/* The scanner sends column, row, then a synchronized touch event. */
	if (event->type == INPUT_EV_ABS && event->code == INPUT_ABS_X) {
		key.col = event->value;
	} else if (event->type == INPUT_EV_ABS && event->code == INPUT_ABS_Y) {
		key.row = event->value;
	} else if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH &&
		   event->sync) {
		key.pressed = event->value != 0;
		k_msgq_put(&key_events, &key, K_FOREVER);
	}
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(MATRIX_NODE), matrix_event, NULL);

int main(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;
	uint32_t held = 0;
	uint32_t pressed = 0;
	uint32_t released = 0;
	struct key_event key;

	if (!device_is_ready(console) || usb_enable(NULL) != 0) {
		return -ENODEV;
	}

	while (!dtr) {
		uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		k_msleep(50);
	}
	if (!device_is_ready(DEVICE_DT_GET(MATRIX_NODE))) {
		printk("FAIL: left matrix scanner initialization\n");
		return -ENODEV;
	}

	printk("READY: Steno Squid left matrix, %d switches\n", KEY_COUNT);
	for (;;) {
		if (k_msgq_get(&key_events, &key, K_SECONDS(5)) == 0) {
			uint32_t bit = BIT(key.row * COLS + key.col);

			if (key.pressed) {
				held |= bit;
				pressed |= bit;
			} else {
				held &= ~bit;
				released |= bit;
			}
			printk("%s R%uC%u ", key.pressed ? "DOWN" : "UP", key.row, key.col);
		} else {
			printk("IDLE ");
		}
		printk("held=0x%06x count=%d tested=%d/%d\n", held, __builtin_popcount(held),
		       __builtin_popcount(pressed & released), KEY_COUNT);
	}
}
