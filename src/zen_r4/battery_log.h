#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
void zen_battery_log_sleep(void);
void zen_battery_log_export(const struct device *console);
void zen_battery_log_editor_command(bool bluetooth);

#ifdef __cplusplus
}
#endif
