#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
void steno_squid_battery_log_sleep(void);
void steno_squid_battery_log_export(const struct device *console);
void steno_squid_battery_log_editor_command(bool bluetooth);

#ifdef __cplusplus
}
#endif
