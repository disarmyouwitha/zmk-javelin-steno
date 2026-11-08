#pragma once

#include <stddef.h>
#include <stdint.h>

#define JAVELIN_HID_REPORT_BYTES 63

#ifdef __cplusplus
extern "C" {
#endif

/* Queue a Bluetooth report without running the engine on the Bluetooth thread. */
int zen_javelin_ble_receive(const void *data, size_t size, uint32_t session);
int zen_javelin_ble_send(const void *data, size_t size, uint32_t session);
int zen_javelin_ble_session_valid(uint32_t session);

#ifdef __cplusplus
}
#endif
