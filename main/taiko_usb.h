#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "taiko_host_config.h"
#include "taiko_reports.h"

esp_err_t taiko_usb_install(taiko_controller_mode_t mode);
bool taiko_usb_send(const taiko_input_snapshot_t *input);

// Host tuning channel. The controller publishes what it is running so a GET
// reads back the truth, and drains host writes on its own task rather than
// letting a USB callback touch a live detector.
void taiko_usb_publish_host_config(const taiko_host_config_t *config, uint8_t status);
bool taiko_usb_take_host_config(taiko_host_config_t *config, uint8_t *command);

// True when build-provisioned credentials were parsed and the asynchronous PS4
// challenge signer is running. Descriptor/report emulation alone returns false.
bool taiko_usb_ps4_authentication_available(void);
