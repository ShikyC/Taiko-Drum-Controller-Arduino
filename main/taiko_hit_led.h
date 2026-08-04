#pragma once

#include "esp_err.h"
#include "taiko_hit_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the low-priority GPIO 38 worker for the eight-pixel channel chain.
// LED failure is isolated from hit detection, so callers may continue
// operating if this returns an error.
esp_err_t taiko_hit_led_start(void);

// Nonblocking notification for a hit accepted by the detector. Each player's
// physical chain order is left Ka, left Don, right Don, right Ka. Every pixel
// has an independent hold window and uses only red (Don) or blue (Ka).
void taiko_hit_led_notify(int player, taiko_zone_t zone);

#ifdef __cplusplus
}
#endif
