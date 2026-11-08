#include "console.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

void zen_javelin_console_line(const char *line, size_t size, IWriter &writer);

namespace {
const device *const serial = DEVICE_DT_GET(DT_NODELABEL(javelin_cdc));
char line[Console::BUFFER_SIZE];

bool connected() {
    uint32_t dtr = 0;
    return uart_line_ctrl_get(serial, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr;
}

class SerialWriter final : public IWriter {
public:
    void Write(const char *text, size_t size) override {
        int64_t deadline = k_uptime_get() + 2000;
        while (size && connected() && k_uptime_get() < deadline) {
            const int sent = uart_fifo_fill(serial, reinterpret_cast<const uint8_t *>(text), size);
            if (sent > 0) {
                text += sent;
                size -= sent;
                deadline = k_uptime_get() + 2000;
            } else {
                k_msleep(1);
            }
        }
    }
};

bool is_command(const char *line, const char *name) {
    const size_t size = strlen(name);
    return strncmp(line, name, size) == 0 &&
           (line[size] == ' ' || line[size] == '\n' || line[size] == '\0');
}

void dispatch(size_t size, SerialWriter &writer) {
    const bool channel = size > 4 && line[0] == 'c' && line[1] >= '0' && line[1] <= '9' &&
                         line[2] >= '0' && line[2] <= '9' && line[3] == ' ';
    const char *command = line + (channel ? 4 : 0);
    if (strcmp(command, "start_javelin_console\n") == 0 || strcmp(command, "\n") == 0) {
        return;
    }

    // Expose dictionary operations only. The browser cannot invoke firmware
    // flashing, arbitrary memory access, scripts, or keyboard configuration.
    const char *allowed[] = {"hello", "list_dictionaries", "lookup_stroke",
                             "add_user_entry", "remove_user_entry", "reset_user_dictionary"};
    for (const char *name : allowed) {
        if (is_command(command, name)) {
            zen_javelin_console_line(line, size, writer);
            return;
        }
    }
    if (strcmp(command, "print_dictionary user_dictionary\n") == 0) {
        zen_javelin_console_line(line, size, writer);
        return;
    }

    if (channel) {
        writer.Write(line, 4);
    }
    constexpr char message[] = "ERR Command unavailable on this keyboard\n\n";
    writer.Write(message, sizeof(message) - 1);
}

void serial_worker(void *, void *, void *) {
    SerialWriter writer;
    size_t used = 0;
    bool too_long = false;

    for (;;) {
        unsigned char c;
        if (!connected()) {
            used = 0;
            too_long = false;
            k_msleep(10);
        } else if (uart_poll_in(serial, &c) != 0) {
            k_msleep(2);
        } else if (c == '\n') {
            if (too_long) {
                if (used >= 4 && line[0] == 'c' && line[3] == ' ') {
                    writer.Write(line, 4);
                }
                constexpr char message[] = "ERR Command too long\n\n";
                writer.Write(message, sizeof(message) - 1);
            } else {
                line[used++] = '\n';
                line[used] = '\0';
                dispatch(used, writer);
            }
            used = 0;
            too_long = false;
        } else if (c != '\r' && c != '\0') {
            if (used + 2 < sizeof(line)) {
                line[used++] = c;
            } else {
                too_long = true;
            }
        }
    }
}
} // namespace

K_THREAD_DEFINE(javelin_web_serial, 4096, serial_worker, nullptr, nullptr, nullptr, 6, 0, 200);
