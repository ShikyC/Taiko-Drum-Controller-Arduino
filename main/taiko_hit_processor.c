#include "taiko_hit_processor.h"

#include <limits.h>
#include <string.h>

#define Q8_ONE 256U
#define BASELINE_TRACK_SHIFT 10
#define BASELINE_TRACK_MARGIN 36U
#define NOISY_PROFILE_REFRACTORY_SAMPLES 200U

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
        .trigger_level = 48,
        .release_level = 34,
        .full_scale_level = 1360,
        .channel_gain_q8 = {
            Q8_ONE,
            2U * Q8_ONE,
            Q8_ONE,
            (3U * Q8_ONE) / 2U,
        },
        .integration_samples = 10,
        .capture_samples = 16,
        .refractory_samples = 125,
        .rearm_samples = 16,
        // 19.2 ms at 10,416.667 scans/s. A 60 fps game poll (16.7 ms) is then
        // guaranteed to land inside the event with ~15% margin, while still
        // clearing the 25 ms consecutive-hit budget the tests pin.
        .output_hold_samples = 200,
        .output_decay_shift = 12,
        .minimum_axis = 8,
        .crosstalk_ratio_q8 = (3U * Q8_ONE) / 2U,
        .attack_ratio_q8 = (5U * Q8_ONE) / 2U,
        .attack_margin = 24,
        .hold_attack_shift = 5,
        .hold_release_shift = 5,
        .mask_samples = 52,
        .crosstalk_mask_samples = 62,
        .min_sharpness_q8 = 64, /* 0.25 */
        .sharpness_samples = 16,
    };

    switch (sensitivity) {
        case TAIKO_SENSITIVITY_SENSITIVE:
            config.noise_floor = 12;
            config.trigger_level = 40;
            config.release_level = 27;
            config.full_scale_level = 1135;
            break;
        case TAIKO_SENSITIVITY_FIRM:
            config.noise_floor = 24;
            config.trigger_level = 70;
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

taiko_hit_config_t taiko_hit_long_tail_config(void) {
    taiko_hit_config_t config =
        taiko_hit_config_for_sensitivity(TAIKO_SENSITIVITY_FIRM);

    // At 10,416.667 complete scans/s this is 19.2 ms. This leaves enough time
    // for capture and USB publication before the 25 ms consecutive-hit limit.
    // Per-zone quiet tracking below rejects the decaying tail without locking
    // the other three zones for this entire interval.
    config.refractory_samples = NOISY_PROFILE_REFRACTORY_SAMPLES;
    // The long-tail drum rings for hundreds of milliseconds with beating
    // humps; demand a sharper amplitude jump over the tail and use a longer
    // envelope memory so a hump never looks like a fresh onset.
    config.crosstalk_mask_samples = 104;
    // The long-tail drum radiates a slow, low-frequency packet into its
    // neighbours for tens of milliseconds after a strike; its genuine hits
    // are softer-spectrum than the standard drum's, so the sharpness gate
    // runs lower and the crosstalk mask runs longer.
    config.min_sharpness_q8 = 46; /* 0.18 */
    config.sharpness_samples = 16;
    // The long-tail drum transfers more energy to the neighbours, so weak
    // swipe hits arrive on sensors that never fully settle; trigger a bit
    // lower and lean on the crosstalk gates instead of raw threshold.
    config.trigger_level = 60;
    config.attack_ratio_q8 = 3U * Q8_ONE;
    config.attack_margin = 32;
    config.hold_attack_shift = 6;
    config.hold_release_shift = 6;
    return config;
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
    if (processor->config.sharpness_samples == 0) {
        processor->config.sharpness_samples = 1;
    } else if (processor->config.sharpness_samples > TAIKO_MAX_INTEGRATION_SAMPLES) {
        processor->config.sharpness_samples = TAIKO_MAX_INTEGRATION_SAMPLES;
    }
    if (processor->config.rearm_samples == 0) {
        processor->config.rearm_samples = 1;
    }
    if (processor->config.output_hold_samples == 0) {
        processor->config.output_hold_samples = 1;
    }
    if (processor->config.output_decay_shift < 1) {
        processor->config.output_decay_shift = 1;
    } else if (processor->config.output_decay_shift > 24) {
        processor->config.output_decay_shift = 24;
    }
    if (processor->config.full_scale_level <= processor->config.trigger_level) {
        processor->config.full_scale_level = processor->config.trigger_level + 1;
    }
    if (processor->config.minimum_axis == 0) {
        processor->config.minimum_axis = 1;
    } else if (processor->config.minimum_axis > 127) {
        processor->config.minimum_axis = 127;
    }
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        processor->zone_armed[channel] = true;
    }
}

static void end_output(taiko_hit_processor_t *processor) {
    processor->output.active = false;
    processor->output.axis_value = 0;
    processor->output_level_q8 = 0;
    processor->output_peak_level = 0;
    processor->output_last_axis = 0;
}

/* Shape the axis while a hit is live instead of freezing one number for the
 * whole hold. A constant is the worst thing to hand this game: it carries no
 * change for the poll to latch onto, and the arcade loader has to fake a
 * varying value for exactly that reason. */
static void update_output_envelope(
    taiko_hit_processor_t *processor,
    const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER]) {
    if (processor->output_remaining == 0) {
        end_output(processor);
        return;
    }
    processor->output_remaining--;
    if (processor->output_remaining == 0) {
        end_output(processor);
        return;
    }

    uint32_t level_q8 = processor->output_level_q8;
    level_q8 -= level_q8 >> processor->config.output_decay_shift;

    // A drum still ringing holds its own envelope up; the droop only takes
    // over once the zone has gone quiet. This is what the ATmega build got
    // from its boxcar, and it is what long-tail drums need.
    const uint32_t live_q8 = (uint32_t)levels[processor->output.zone] << 8;
    if (live_q8 > level_q8) {
        level_q8 = live_q8;
    }
    // Never report harder than the onset actually was: a later beating hump
    // on the struck zone must not inflate the force the game classifies.
    const uint32_t peak_q8 = (uint32_t)processor->output_peak_level << 8;
    if (level_q8 > peak_q8) {
        level_q8 = peak_q8;
    }
    processor->output_level_q8 = level_q8;

    uint8_t axis =
        level_to_axis(&processor->config, (uint16_t)(level_q8 >> 8));
    // Insurance for the quantised tail, where the droop can be less than one
    // axis step between polls: offset by one so the pair still differs. The
    // next scan compares against this value, so it settles into an
    // alternation rather than drifting.
    if (axis == processor->output_last_axis) {
        axis = axis > processor->config.minimum_axis ? (uint8_t)(axis - 1)
                                                     : (uint8_t)(axis + 1);
    }
    processor->output_last_axis = axis;
    processor->output.axis_value = axis;
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
    const uint8_t sharpness_samples = processor->config.sharpness_samples;
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
        /* Instantaneous amplitude, kept for the crosstalk comparison: an
         * onset peaks on the struck sensor one or two samples before the
         * neighbours' RMS levels react, which is exactly the margin the
         * dominance gate needs. */
        processor->raw_amplitude[channel] =
            amplitude > UINT16_MAX ? UINT16_MAX : (uint16_t)amplitude;

        /* Onset sharpness memory: squared per-sample change of the rectified
         * amplitude. Stick strikes are broadband (large diffs); the slow
         * travelling packet a long-tail drum radiates is not. */
        const int32_t diff =
            (int32_t)amplitude - (int32_t)processor->prev_amplitude[channel];
        const uint64_t diff_energy = (uint64_t)((int64_t)diff * diff);
        processor->diff_sum[channel] -=
            processor->diff_ring[channel][processor->diff_ring_index];
        processor->diff_ring[channel][processor->diff_ring_index] = diff_energy;
        processor->diff_sum[channel] += diff_energy;
        processor->prev_amplitude[channel] =
            amplitude > UINT16_MAX ? UINT16_MAX : (uint16_t)amplitude;
    }

    processor->ring_index = (uint8_t)((processor->ring_index + 1) % integration_samples);
    if (processor->ring_count < integration_samples) {
        processor->ring_count++;
    }
    processor->diff_ring_index =
        (uint8_t)((processor->diff_ring_index + 1) % sharpness_samples);
    if (processor->diff_ring_count < sharpness_samples) {
        processor->diff_ring_count++;
    }
}

/* Peak-hold envelope per level for onset (attack) gating: near-instant
 * attack, slow exponential release. */
static void update_hold_envelopes(
    taiko_hit_processor_t *processor,
    const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER]) {
    const uint8_t attack_shift =
        processor->config.hold_attack_shift < 1
            ? 1
            : processor->config.hold_attack_shift;
    const uint8_t release_shift =
        processor->config.hold_release_shift < 1
            ? 1
            : processor->config.hold_release_shift;
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        const int32_t delta =
            (int32_t)((uint32_t)levels[channel] << 8) -
            (int32_t)processor->hold_env_q8[channel];
        const uint8_t shift = delta > 0 ? attack_shift : release_shift;
        processor->hold_env_q8[channel] =
            (uint32_t)((int32_t)processor->hold_env_q8[channel] +
                       (delta >> shift));
    }
}

static bool find_winner(const taiko_hit_processor_t *processor,
                        const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER],
                        taiko_zone_t *winning_zone,
                        uint16_t *winning_level) {
    bool found = false;
    *winning_level = 0;
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (processor->zone_armed[channel] &&
            (!found || levels[channel] > *winning_level)) {
            found = true;
            *winning_zone = (taiko_zone_t)channel;
            *winning_level = levels[channel];
        }
    }
    return found;
}

/* True when this channel's level exceeds every competing channel's level by
 * the configured crosstalk ratio. Competitors are armed channels (possible
 * winners) plus channels still inside their refractory: a neighbor ringing
 * from a strike we just emitted must keep suppressing crosstalk candidates on
 * the other sensors. Channels whose refractory has long expired only carry a
 * decaying tail, which the attack gate rejects on its own merits - including
 * them here would also block genuine weaker hits into a still-ringing drum. */
static bool dominates_crosstalk(const taiko_hit_processor_t *processor,
                                size_t zone) {
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (channel == zone) {
            continue;
        }
        if (!processor->zone_armed[channel] &&
            processor->refractory_remaining[channel] == 0) {
            continue;
        }
        if ((uint32_t)processor->raw_amplitude[zone] * Q8_ONE <
            (uint32_t)processor->raw_amplitude[channel] *
                processor->config.crosstalk_ratio_q8) {
            return false;
        }
    }
    return true;
}

/* True when the level is rising fast enough to be a fresh onset: it must
 * exceed a slow lagged copy of the envelope by the attack ratio. Decaying
 * tails and slow beating humps stay near their own lagged envelope and fail. */
/* Spectral-tilt gate: the RMS per-sample change of the amplitude must be at
 * least min_sharpness_q8/256 of the channel's level. Both quantities carry
 * the channel gain, so the ratio is gain-free. */
static bool passes_sharpness_gate(const taiko_hit_processor_t *processor,
                                  const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER],
                                  size_t zone) {
    if (processor->diff_ring_count == 0) {
        return false;
    }
    const uint32_t diff_rms = integer_sqrt_u64(
        processor->diff_sum[zone] / processor->diff_ring_count);
    return (uint64_t)diff_rms * Q8_ONE >=
           (uint64_t)processor->config.min_sharpness_q8 * levels[zone];
}

static bool passes_attack_gate(const taiko_hit_processor_t *processor,
                               const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER],
                               size_t zone) {
    const uint32_t hold_q8 = processor->hold_env_q8[zone];
    return (uint32_t)levels[zone] * Q8_ONE >=
           ((hold_q8 * processor->config.attack_ratio_q8) >> 8) +
               (uint32_t)processor->config.attack_margin * Q8_ONE;
}

static void update_zone_rearm(
    taiko_hit_processor_t *processor,
    const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER]) {
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (processor->refractory_remaining[channel] > 0) {
            processor->refractory_remaining[channel]--;
        }
        if (levels[channel] <= processor->config.release_level) {
            if (processor->quiet_samples[channel] < UINT8_MAX) {
                processor->quiet_samples[channel]++;
            }
        } else {
            processor->quiet_samples[channel] = 0;
        }
        if (processor->refractory_remaining[channel] == 0 &&
            processor->quiet_samples[channel] >= processor->config.rearm_samples) {
            processor->zone_armed[channel] = true;
        }
    }
}

static bool emit_candidate(
    taiko_hit_processor_t *processor,
    const uint16_t levels[TAIKO_CHANNELS_PER_PLAYER],
    taiko_hit_event_t *event) {
    const uint8_t axis_value =
        level_to_axis(&processor->config, processor->candidate_level);
    processor->output = (taiko_hit_output_t){
        .active = true,
        .zone = processor->candidate_zone,
        .axis_value = axis_value,
    };
    processor->output_remaining = processor->config.output_hold_samples;
    processor->output_peak_level = processor->candidate_level;
    processor->output_level_q8 = (uint32_t)processor->candidate_level << 8;
    processor->output_last_axis = axis_value;
    processor->detector_state = TAIKO_DETECTOR_IDLE;
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        processor->zone_mask_remaining[channel] =
            channel == (size_t)processor->candidate_zone
                ? processor->config.mask_samples
                : processor->config.crosstalk_mask_samples;
    }

    // Disarm every channel that is still participating in this vibration.
    // Each channel rearms independently after it becomes quiet, so a long tail
    // on (for example) right Don cannot suppress a later left Don strike.
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (levels[channel] > processor->config.release_level) {
            processor->zone_armed[channel] = false;
            processor->quiet_samples[channel] = 0;
        }
    }
    processor->zone_armed[processor->candidate_zone] = false;
    processor->quiet_samples[processor->candidate_zone] = 0;
    processor->refractory_remaining[processor->candidate_zone] =
        processor->config.refractory_samples;

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
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        if (processor->zone_mask_remaining[channel] > 0) {
            processor->zone_mask_remaining[channel]--;
        }
    }

    uint16_t levels[TAIKO_CHANNELS_PER_PLAYER];
    update_baselines_and_levels(processor, samples, levels);
    // Ordered after the levels so the envelope can track them, and before the
    // state machine so a fresh emission this scan overrides it.
    update_output_envelope(processor, levels);
    update_hold_envelopes(processor, levels);
    update_zone_rearm(processor, levels);

    taiko_zone_t winner = TAIKO_ZONE_LEFT_DON;
    uint16_t winning_level = 0;
    const bool winner_found =
        find_winner(processor, levels, &winner, &winning_level);

    switch (processor->detector_state) {
        case TAIKO_DETECTOR_IDLE:
            if (winner_found &&
                winning_level >= processor->config.trigger_level &&
                processor->zone_mask_remaining[winner] == 0 &&
                dominates_crosstalk(processor, winner) &&
                passes_attack_gate(processor, levels, winner) &&
                passes_sharpness_gate(processor, levels, winner)) {
                processor->detector_state = TAIKO_DETECTOR_CAPTURE;
                processor->capture_remaining = processor->config.capture_samples;
                processor->candidate_zone = winner;
                processor->candidate_level = winning_level;
            }
            break;

        case TAIKO_DETECTOR_CAPTURE:
            /* Keep the strongest dominating onset within the capture window;
             * a neighbor that merely rides the struck zone's tail can still
             * peak higher and must not steal the event. */
            if (winner_found && winning_level > processor->candidate_level &&
                dominates_crosstalk(processor, winner)) {
                processor->candidate_zone = winner;
                processor->candidate_level = winning_level;
            }
            if (--processor->capture_remaining == 0) {
                return emit_candidate(processor, levels, event);
            }
            break;
    }

    return false;
}

taiko_hit_output_t taiko_hit_processor_get_output(const taiko_hit_processor_t *processor) {
    return processor->output;
}
