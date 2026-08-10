#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "taiko_reports.h"

esp_err_t taiko_usb_install(taiko_controller_mode_t mode);
bool taiko_usb_send(const taiko_input_snapshot_t *input);

// True when build-provisioned credentials were parsed and the asynchronous PS4
// challenge signer is running. Descriptor/report emulation alone returns false.
bool taiko_usb_ps4_authentication_available(void);
