#include "console.h"
#include "javelin_hid_transport.h"
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
#include "battery_log.h"
#endif

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/usb_hid.h>

extern "C" bool zmk_usb_is_hid_ready(void);

// False when the engine could not take the command, so nothing was written.
bool zen_javelin_console_line(const char *line, size_t size, IWriter &writer);

namespace {
constexpr size_t REPORT_BYTES = JAVELIN_HID_REPORT_BYTES;
const device *hid;
K_SEM_DEFINE(transmit_ready, 1, 1);

struct Report {
    uint8_t bytes[REPORT_BYTES];
    bool bluetooth = false;
    uint32_t session = 0;
};
K_MSGQ_DEFINE(received_reports, sizeof(Report), 40, 4);
alignas(4) Report transmit_report;

// Javelin's vendor console collection, separate from keyboard and Plover HID.
constexpr uint8_t descriptor[] = {
    0x06, 0x31, 0xff, // Usage Page 0xFF31
    0x09, 0x74,       // Usage 0x74
    0xa1, 0x01,       // Application collection
    0x15, 0x00,       // Logical minimum 0
    0x26, 0xff, 0x00, // Logical maximum 255
    0x75, 0x08,       // Eight bits per field
    0x95, REPORT_BYTES,
    0x09, 0x75,
    0x81, 0x02,       // Input, data, variable
    0x95, REPORT_BYTES,
    0x09, 0x76,
    0x91, 0x02,       // Output, data, variable
    0xc0,
};

void input_complete(const device *) {
    k_sem_give(&transmit_ready);
}

int output_report(const device *, usb_setup_packet *setup, int32_t *size, uint8_t **data) {
    if (sys_le16_to_cpu(setup->wValue) != 0x0200 || *size <= 0 ||
        *size > REPORT_BYTES) {
        return -EINVAL;
    }
    Report report{};
    memcpy(report.bytes, *data, *size);
    return k_msgq_put(&received_reports, &report, K_NO_WAIT);
}

const hid_ops operations = {
    .set_report = output_report,
    .int_in_ready = input_complete,
};

int initialize_hid() {
    hid = device_get_binding("HID_2");
    if (!hid) {
        return -ENODEV;
    }
    usb_hid_register_device(hid, descriptor, sizeof(descriptor), &operations);
    return usb_hid_init(hid);
}

class HidWriter final : public IWriter {
public:
    explicit HidWriter(const Report &report)
        : bluetooth(report.bluetooth), session(report.session) {}

#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
    void CountCommand() const { zen_battery_log_editor_command(bluetooth); }
#endif

    void Write(const char *text, size_t size) override {
        if (bluetooth) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BLE_HID
            while (size && !failed) {
                uint8_t packet[REPORT_BYTES]{};
                const size_t count = MIN(size, REPORT_BYTES);
                memcpy(packet, text, count);
                if (zen_javelin_ble_send(packet, sizeof(packet), session) != 0) {
                    failed = true;
                    return;
                }
                text += count;
                size -= count;
            }
#endif
            return;
        }
        while (size && hid && zmk_usb_is_hid_ready()) {
            if (k_sem_take(&transmit_ready, K_SECONDS(2)) != 0) {
                return;
            }
            memset(&transmit_report, 0, sizeof(transmit_report));
            const size_t count = MIN(size, REPORT_BYTES);
            memcpy(transmit_report.bytes, text, count);
            const int rc = hid_int_ep_write(hid, transmit_report.bytes,
                                            sizeof(transmit_report.bytes), nullptr);
            if (rc != 0) {
                k_sem_give(&transmit_ready);
                return;
            }
            text += count;
            size -= count;
        }
    }
private:
    bool bluetooth;
    uint32_t session;
    bool failed = false;
};

bool is_command(const char *line, const char *name) {
    const size_t size = strlen(name);
    return strncmp(line, name, size) == 0 &&
           (line[size] == ' ' || line[size] == '\n' || line[size] == '\0');
}

void dispatch(char *line, size_t size, HidWriter &writer) {
    const bool channel = size > 4 && line[0] == 'c' && line[1] >= '0' && line[1] <= '9' &&
                         line[2] >= '0' && line[2] <= '9' && line[3] == ' ';
    const char *command = line + (channel ? 4 : 0);
    if (strcmp(command, "start_javelin_console\n") == 0 || strcmp(command, "\n") == 0) {
        return;
    }
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
    writer.CountCommand();
#endif

    // Expose dictionary operations and selection of built-in host layouts.
    const char *allowed[] = {"hello", "list_dictionaries", "lookup_stroke",
                             "add_user_entry", "remove_user_entry", "reset_user_dictionary",
                             "enable_dictionary", "disable_dictionary", "toggle_dictionary",
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
                             "get_parameter", "set_host_layout",
#endif
    };
    // The Active Dictionaries page updates its checkboxes from status events
    // after an enable/disable reply. Allow its subscription over this HID link.
    bool forward = strcmp(command, "print_dictionary user_dictionary\n") == 0 ||
                   strcmp(command, "enable_events dictionary_status\n") == 0 ||
                   strcmp(command, "disable_events dictionary_status\n") == 0;
    for (const char *name : allowed) {
        forward = forward || is_command(command, name);
    }
    if (forward && zen_javelin_console_line(line, size, writer)) {
        return;
    }

    if (channel) {
        writer.Write(line, 4);
    }
    // Always answer, so the browser never waits on a command that was dropped.
    const char *message = forward ? "ERR Dictionary flash is not ready\n\n"
                                  : "ERR Command unavailable on this keyboard\n\n";
    writer.Write(message, strlen(message));
}

struct CommandBuffer {
    int channel = -2;
    size_t used = 0;
    bool too_long = false;
    int64_t last_packet = 0;
    bool bluetooth = false;
    uint32_t session = 0;
    char line[Console::BUFFER_SIZE];
};
CommandBuffer buffers[4];

void reset_buffer(CommandBuffer &buffer) {
    buffer.channel = -2;
    buffer.used = 0;
    buffer.too_long = false;
    buffer.last_packet = 0;
}

void hid_worker(void *, void *, void *) {
    Report report;

    for (;;) {
        if (!zmk_usb_is_hid_ready()) {
            k_sem_give(&transmit_ready);
        }
        for (auto &buffer : buffers) {
            if (!buffer.bluetooth && !zmk_usb_is_hid_ready()) {
                reset_buffer(buffer);
            }
        }
        if (k_msgq_get(&received_reports, &report, K_MSEC(100)) != 0) {
            continue;
        }
        if (report.bluetooth) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BLE_HID
            if (!zen_javelin_ble_session_valid(report.session)) {
                continue;
            }
#else
            continue;
#endif
        } else if (!zmk_usb_is_hid_ready()) {
            k_sem_give(&transmit_ready);
            continue;
        }
        HidWriter writer(report);

        const auto *bytes = report.bytes;
        size_t length = sizeof(report.bytes);
        int channel = -1;
        const bool has_channel = bytes[0] == 'c' && bytes[1] >= '0' && bytes[1] <= '9' &&
                                 bytes[2] >= '0' && bytes[2] <= '9' && bytes[3] == ' ';
        if (has_channel) {
            channel = 10 * (bytes[1] - '0') + bytes[2] - '0';
            bytes += 4;
            length -= 4;
        }

        CommandBuffer *target = nullptr;
        for (auto &buffer : buffers) {
            if (buffer.channel == channel && buffer.bluetooth == report.bluetooth &&
                buffer.session == report.session) {
                target = &buffer;
                break;
            }
            if (buffer.channel == -2 || k_uptime_get() - buffer.last_packet > 5000) {
                target = &buffer;
            }
        }
        if (!target) {
            if (has_channel) {
                writer.Write(reinterpret_cast<const char *>(report.bytes), 4);
            }
            constexpr char busy[] = "ERR Too many console channels\n\n";
            writer.Write(busy, sizeof(busy) - 1);
            continue;
        }
        if (target->channel != channel || target->bluetooth != report.bluetooth ||
            target->session != report.session || k_uptime_get() - target->last_packet > 5000) {
            reset_buffer(*target);
            target->channel = channel;
            target->bluetooth = report.bluetooth;
            target->session = report.session;
        }
        target->last_packet = k_uptime_get();

        for (size_t i = 0; i < length; i++) {
            const uint8_t c = bytes[i];
            if (c == 0 || c == '\r') {
                continue;
            }
            if (target->used == 0 && has_channel) {
                memcpy(target->line, report.bytes, 4);
                target->used = 4;
            }
            if (c == '\n') {
                if (target->too_long) {
                    if (has_channel) {
                        writer.Write(reinterpret_cast<const char *>(report.bytes), 4);
                    }
                    constexpr char message[] = "ERR Command too long\n\n";
                    writer.Write(message, sizeof(message) - 1);
                } else {
                    target->line[target->used++] = '\n';
                    target->line[target->used] = '\0';
                    dispatch(target->line, target->used, writer);
                }
                target->used = 0;
                target->too_long = false;
            } else if (target->used + 2 < sizeof(target->line)) {
                target->line[target->used++] = c;
            } else {
                target->too_long = true;
            }
        }
        if (target->used == 0) {
            target->channel = -2;
        }
    }
}
} // namespace

#ifdef CONFIG_ZMK_JAVELIN_STENO_BLE_HID
extern "C" int zen_javelin_ble_receive(const void *data, size_t size, uint32_t session) {
    if (!size || size > REPORT_BYTES) {
        return -EINVAL;
    }
    Report report{};
    report.bluetooth = true;
    report.session = session;
    memcpy(report.bytes, data, size);
    return k_msgq_put(&received_reports, &report, K_NO_WAIT);
}
#endif

SYS_INIT(initialize_hid, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
K_THREAD_DEFINE(javelin_web_hid, 4096, hid_worker, nullptr, nullptr, nullptr, 6, 0, 200);
