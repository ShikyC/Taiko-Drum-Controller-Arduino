#pragma once

#include "esp_err.h"
#include "taiko_hit_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the low-priority GPIO 38 indicator worker. LED failure is isolated
// from hit detection, so callers may continue operating if this returns an
// error.
esp_err_t taiko_hit_led_start(void);

// Nonblocking notification for a hit accepted by the detector. Don and Ka
// use independent hold windows; when both are active the shared LED is purple.
void taiko_hit_led_notify(taiko_zone_t zone);

#ifdef __cplusplus
}
#endif
