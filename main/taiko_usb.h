#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "taiko_reports.h"

esp_err_t taiko_usb_install(taiko_controller_mode_t mode);
bool taiko_usb_send(const taiko_input_snapshot_t *input);

// A PS4 accepts normal reports only while its licensed authentication
// challenge is being answered. The current board has no credential or donor
// controller path, so this remains false even though PS4 descriptors and
// reports are available for development and host-side testing.
bool taiko_usb_ps4_authentication_available(void);
