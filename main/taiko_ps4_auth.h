#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Initializes optional build-provisioned PS4 credentials. ESP_ERR_NOT_SUPPORTED
// means the firmware was built without credentials; PS4-shaped HID reports can
// still enumerate, but a console session will retain the eight-minute timeout.
esp_err_t taiko_ps4_auth_init(void);
bool taiko_ps4_auth_available(void);

// Feature-report payloads exclude the HID report ID. These functions are safe
// to call from TinyUSB's device task; RSA work is delegated to a worker task.
bool taiko_ps4_auth_set_nonce(const uint8_t *payload, size_t payload_size);
size_t taiko_ps4_auth_get_signature(uint8_t *payload, size_t payload_size);
size_t taiko_ps4_auth_get_status(uint8_t *payload, size_t payload_size);
void taiko_ps4_auth_reset(void);
