#include "taiko_hit_processor.h"

#include <limits.h>
#include <string.h>

#define Q8_ONE 256U
#define BASELINE_TRACK_SHIFT 10
#define BASELINE_TRACK_MARGIN 36U

static uint32_t integer_sqrt_u64(uint64_t value) {
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

static uint8_t level_to_axis(const taiko_hit_config_t *config, uint16_t level) {
    if (level >= config->full_scale_level) {
        return 127;
    }
    if (level <= config->trigger_level ||
        config->full_scale_level <= config->trigger_level) {
        return config->minimum_axis;
    }

    const uint32_t input_range = config->full_scale_level - config->trigger_level;
    const uint32_t output_range = 127U - config->minimum_axis;
    const uint32_t scaled =
        ((uint32_t)(level - config->trigger_level) * output_range + input_range / 2U) /
        input_range;
    return (uint8_t)(config->minimum_axis + scaled);
}

taiko_hit_config_t taiko_hit_config_for_sensitivity(
    taiko_sensitivity_t sensitivity) {
    taiko_hit_config_t config = {
        .noise_floor = 18,
        .trigger_level = 68,
        .release_level = 34,
        .full_scale_level = 1360,
        .channel_gain_q8 = {
            Q8_ONE,
            2U * Q8_ONE,
            Q8_ONE,
            (3U * Q8_ONE) / 2U,
        },
        .integration_samples = 10,
        .capture_samples = 8,
        .refractory_samples = 125,
        .rearm_samples = 16,
        .output_hold_samples = 125,
        .minimum_axis = 8,
    };

    switch (sensitivity) {
        case TAIKO_SENSITIVITY_SENSITIVE:
            config.noise_floor = 12;
            config.trigger_level = 53;
            config.release_level = 27;
            config.full_scale_level = 1135;
            break;
        case TAIKO_SENSITIVITY_FIRM:
            config.noise_floor = 24;
            config.trigger_level = 98;
            config.release_level = 49;
            config.full_scale_level = 1665;
            break;
        case TAIKO_SENSITIVITY_BALANCED:
        default:
            break;
    }
    return config;
}

taiko_hit_config_t taiko_hit_default_config(void) {
    return taiko_hit_config_for_sensitivity(TAIKO_SENSITIVITY_BALANCED);
}

void taiko_hit_processor_init(taiko_hit_processor_t *processor,
                              const taiko_hit_config_t *config) {
    memset(processor, 0, sizeof(*processor));
    processor->config = config != NULL ? *config : taiko_hit_default_config();

    if (processor->config.integration_samples == 0) {
        processor->config.integration_samples = 1;
    } else if (processor->config.integration_samples > TAIKO_MAX_INTEGRATION_SAMPLES) {
        processor->config.integration_samples = TAIKO_MAX_INTEGRATION_SAMPLES;
    }
    if (processor->config.capture_samples == 0) {
        processor->config.capture_samples = 1;
    }
    if (processor->config.rearm_samples == 0) {
        processor->config.rearm_samples = 1;
    }
    if (processor->config.output_hold_samples == 0) {
        processor->config.output_hold_samples = 1;
    }
    if (processor->config.full_scale_level <= processor->config.trigger_level) {
        processor->config.full_scale_level = processor->config.trigger_level + 1;
    }
    if (processor->config.minimum_axis == 0) {
        processor->config.minimum_axis = 1;
    } else if (processor->config.minimum_axis > 127) {
        processor->config.minimum_axis = 127;
    }
}

static void update_output_hold(taiko_hit_processor_t *processor) {
    if (processor->output_remaining == 0) {
        processor->output.active = false;
        processor->output.axis_value = 0;
        return;
    }
    processor->output_remaining--;
    if (processor->output_remaining == 0) {
        processor->output.active = false;
        processor->output.axis_value = 0;
    }
}

static void update_baselines_and_levels(
    taiko_hit_processor_t *processor,
    const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER],
    uint16_t levels[TAIKO_CHANNELS_PER_PLAYER]) {
    if (!processor->baseline_initialized) {
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            processor->baseline_q8[channel] = (int32_t)samples[channel] << 8;
        }
        processor->baseline_initialized = true;
    }

    const uint8_t integration_samples = processor->config.integration_samples;
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        const int32_t raw_q8 = (int32_t)samples[channel] << 8;
        const uint16_t baseline = (uint16_t)(processor->baseline_q8[channel] >> 8);

        if (samples[channel] <= baseline + BASELINE_TRACK_MARGIN) {
            processor->baseline_q8[channel] +=
                (raw_q8 - processor->baseline_q8[channel]) >> BASELINE_TRACK_SHIFT;
        }

        uint32_t amplitude =
            samples[channel] > baseline ? (uint32_t)samples[channel] - baseline : 0;
        amplitude =
            amplitude > processor->config.noise_floor
                ? amplitude - processor->config.noise_floor
                : 0;
        amplitude =
            (amplitude * processor->config.channel_gain_q8[channel] + Q8_ONE / 2U) /
            Q8_ONE;

        const uint64_t energy = (uint64_t)amplitude * amplitude;
        processor->energy_sum[channel] -=
            processor->energy_ring[channel][processor->ring_index];
        processor->energy_ring[channel][processor->ring_index] = energy;
        processor->energy_sum[channel] += energy;

        const uint8_t divisor =
            processor->ring_count < integration_samples
                ? (uint8_t)(processor->ring_count + 1)
                : integration_samples;
        uint32_t level = integer_sqrt_u64(processor->energy_sum[channel] / divisor);
        levels[channel] = level > UINT16_MAX ? UINT16_MAX : (uint16_t)level;
    }

    processor->ring_index = (uint8_t)((processor->ring_index + 1) % integration_samples);
    if (processor->ring_count < integration_samples) {
        processor->ring_count++;
    }
}

static taiko_zone_t find_winner(const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER],
                                uint16_t *winning_level) {
    taiko_zone_t winner = TAIKO_ZONE_LEFT_DON;
    for (size_t channel = 1; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (levels[channel] > levels[winner]) {
            winner = (taiko_zone_t)channel;
        }
    }
    *winning_level = levels[winner];
    return winner;
}

static bool emit_candidate(taiko_hit_processor_t *processor, taiko_hit_event_t *event) {
    const uint8_t axis_value =
        level_to_axis(&processor->config, processor->candidate_level);
    processor->output = (taiko_hit_output_t){
        .active = true,
        .zone = processor->candidate_zone,
        .axis_value = axis_value,
    };
    processor->output_remaining = processor->config.output_hold_samples;
    processor->detector_state = TAIKO_DETECTOR_REFRACTORY;
    processor->refractory_remaining = processor->config.refractory_samples;
    processor->quiet_samples = 0;

    if (event != NULL) {
        *event = (taiko_hit_event_t){
            .zone = processor->candidate_zone,
            .axis_value = axis_value,
            .level = processor->candidate_level,
        };
    }
    return true;
}

bool taiko_hit_processor_push(taiko_hit_processor_t *processor,
                              const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER],
                              taiko_hit_event_t *event) {
    update_output_hold(processor);

    uint16_t levels[TAIKO_CHANNELS_PER_PLAYER];
    update_baselines_and_levels(processor, samples, levels);

    uint16_t winning_level = 0;
    const taiko_zone_t winner = find_winner(levels, &winning_level);

    switch (processor->detector_state) {
        case TAIKO_DETECTOR_IDLE:
            if (winning_level >= processor->config.trigger_level) {
                processor->detector_state = TAIKO_DETECTOR_CAPTURE;
                processor->capture_remaining = processor->config.capture_samples;
                processor->candidate_zone = winner;
                processor->candidate_level = winning_level;
            }
            break;

        case TAIKO_DETECTOR_CAPTURE:
            if (winning_level > processor->candidate_level) {
                processor->candidate_zone = winner;
                processor->candidate_level = winning_level;
            }
            if (--processor->capture_remaining == 0) {
                return emit_candidate(processor, event);
            }
            break;

        case TAIKO_DETECTOR_REFRACTORY:
            if (processor->refractory_remaining > 0) {
                processor->refractory_remaining--;
            }
            if (winning_level <= processor->config.release_level) {
                if (processor->quiet_samples < UINT8_MAX) {
                    processor->quiet_samples++;
                }
            } else {
                processor->quiet_samples = 0;
            }
            if (processor->refractory_remaining == 0 &&
                processor->quiet_samples >= processor->config.rearm_samples) {
                processor->detector_state = TAIKO_DETECTOR_IDLE;
            }
            break;
    }

    return false;
}

taiko_hit_output_t taiko_hit_processor_get_output(const taiko_hit_processor_t *processor) {
    return processor->output;
}
