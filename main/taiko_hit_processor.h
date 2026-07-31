#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAIKO_CHANNELS_PER_PLAYER 4
#define TAIKO_MAX_INTEGRATION_SAMPLES 16

typedef enum {
    TAIKO_ZONE_LEFT_DON = 0,
    TAIKO_ZONE_LEFT_KA = 1,
    TAIKO_ZONE_RIGHT_DON = 2,
    TAIKO_ZONE_RIGHT_KA = 3,
} taiko_zone_t;

typedef enum {
    TAIKO_SENSITIVITY_SENSITIVE = 0,
    TAIKO_SENSITIVITY_BALANCED,
    TAIKO_SENSITIVITY_FIRM,
} taiko_sensitivity_t;

typedef struct {
    uint16_t noise_floor;
    uint16_t trigger_level;
    uint16_t release_level;
    uint16_t full_scale_level;
    uint16_t channel_gain_q8[TAIKO_CHANNELS_PER_PLAYER];
    uint8_t integration_samples;
    uint8_t capture_samples;
    uint16_t refractory_samples;
    uint8_t rearm_samples;
    uint16_t output_hold_samples;
    uint8_t minimum_axis;
} taiko_hit_config_t;

typedef struct {
    taiko_zone_t zone;
    uint8_t axis_value;
    uint16_t level;
} taiko_hit_event_t;

typedef struct {
    bool active;
    taiko_zone_t zone;
    uint8_t axis_value;
} taiko_hit_output_t;

typedef enum {
    TAIKO_DETECTOR_IDLE = 0,
    TAIKO_DETECTOR_CAPTURE,
    TAIKO_DETECTOR_REFRACTORY,
} taiko_detector_state_t;

typedef struct {
    taiko_hit_config_t config;
    int32_t baseline_q8[TAIKO_CHANNELS_PER_PLAYER];
    uint64_t energy_ring[TAIKO_CHANNELS_PER_PLAYER][TAIKO_MAX_INTEGRATION_SAMPLES];
    uint64_t energy_sum[TAIKO_CHANNELS_PER_PLAYER];
    uint8_t ring_index;
    uint8_t ring_count;
    bool baseline_initialized;

    taiko_detector_state_t detector_state;
    uint8_t capture_remaining;
    uint16_t refractory_remaining;
    uint8_t quiet_samples;
    taiko_zone_t candidate_zone;
    uint16_t candidate_level;

    taiko_hit_output_t output;
    uint16_t output_remaining;
} taiko_hit_processor_t;

taiko_hit_config_t taiko_hit_config_for_sensitivity(
    taiko_sensitivity_t sensitivity);
taiko_hit_config_t taiko_hit_long_tail_config(void);
taiko_hit_config_t taiko_hit_default_config(void);
void taiko_hit_processor_init(taiko_hit_processor_t *processor,
                              const taiko_hit_config_t *config);
bool taiko_hit_processor_push(taiko_hit_processor_t *processor,
                              const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER],
                              taiko_hit_event_t *event);
taiko_hit_output_t taiko_hit_processor_get_output(const taiko_hit_processor_t *processor);

#ifdef __cplusplus
}
#endif
