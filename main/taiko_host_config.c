#include "taiko_host_config.h"

#include <string.h>

#define FIELDS_PER_PLAYER (TAIKO_CHANNELS_PER_PLAYER + 4U)
#define HEADER_BYTES 4U

_Static_assert(HEADER_BYTES +
                       TAIKO_HOST_CONFIG_PLAYERS * FIELDS_PER_PLAYER * 2U ==
                   TAIKO_HOST_CONFIG_REPORT_SIZE,
               "Host config report size does not match its field layout");

static uint16_t clamp_u16(uint32_t value, uint32_t low, uint32_t high) {
    if (value < low) {
        return (uint16_t)low;
    }
    if (value > high) {
        return (uint16_t)high;
    }
    return (uint16_t)value;
}

static void put_u16(uint8_t *buffer, size_t index, uint16_t value) {
    buffer[index] = (uint8_t)(value & 0xFFU);
    buffer[index + 1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16(const uint8_t *buffer, size_t index) {
    return (uint16_t)((uint16_t)buffer[index] | ((uint16_t)buffer[index + 1] << 8));
}

void taiko_host_config_defaults(taiko_host_config_t *config) {
    const taiko_hit_config_t standard = taiko_hit_default_config();
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        taiko_host_config_capture(&standard, &config->player[player]);
    }
}

void taiko_host_config_clamp(taiko_host_config_t *config) {
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        taiko_player_tuning_t *tuning = &config->player[player];
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            tuning->channel_gain_q8[channel] = clamp_u16(
                tuning->channel_gain_q8[channel], TAIKO_GAIN_Q8_MIN, TAIKO_GAIN_Q8_MAX);
        }
        tuning->noise_floor = clamp_u16(tuning->noise_floor, 0, TAIKO_NOISE_FLOOR_MAX);
        // Ordered outermost first: the trigger is bounded by the full scale and
        // the release by the trigger, so a hostile report cannot invert them.
        tuning->full_scale_level = clamp_u16(
            tuning->full_scale_level, TAIKO_FULL_SCALE_MIN, TAIKO_FULL_SCALE_MAX);
        tuning->trigger_level = clamp_u16(tuning->trigger_level, TAIKO_TRIGGER_MIN,
                                          tuning->full_scale_level - 1U);
        tuning->release_level = clamp_u16(tuning->release_level, TAIKO_RELEASE_MIN,
                                          tuning->trigger_level - 1U);
    }
}

size_t taiko_host_config_serialize(const taiko_host_config_t *config,
                                   uint8_t status, uint8_t *buffer, size_t size) {
    if (size < TAIKO_HOST_CONFIG_REPORT_SIZE) {
        return 0;
    }
    memset(buffer, 0, TAIKO_HOST_CONFIG_REPORT_SIZE);
    buffer[0] = TAIKO_HOST_CONFIG_VERSION;
    buffer[1] = 0; /* command is meaningless on read-back */
    buffer[2] = status;
    buffer[3] = 0;

    size_t index = HEADER_BYTES;
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        const taiko_player_tuning_t *tuning = &config->player[player];
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            put_u16(buffer, index, tuning->channel_gain_q8[channel]);
            index += 2;
        }
        put_u16(buffer, index, tuning->noise_floor);
        index += 2;
        put_u16(buffer, index, tuning->trigger_level);
        index += 2;
        put_u16(buffer, index, tuning->release_level);
        index += 2;
        put_u16(buffer, index, tuning->full_scale_level);
        index += 2;
    }
    return TAIKO_HOST_CONFIG_REPORT_SIZE;
}

bool taiko_host_config_deserialize(const uint8_t *buffer, size_t size,
                                   taiko_host_config_t *config, uint8_t *command) {
    if (buffer == NULL || size < TAIKO_HOST_CONFIG_REPORT_SIZE ||
        buffer[0] != TAIKO_HOST_CONFIG_VERSION) {
        return false;
    }

    const uint8_t requested = buffer[1];
    if (requested == TAIKO_HOST_CONFIG_CMD_RESET) {
        taiko_host_config_defaults(config);
        *command = requested;
        return true;
    }

    taiko_host_config_t parsed;
    size_t index = HEADER_BYTES;
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        taiko_player_tuning_t *tuning = &parsed.player[player];
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            tuning->channel_gain_q8[channel] = get_u16(buffer, index);
            index += 2;
        }
        tuning->noise_floor = get_u16(buffer, index);
        index += 2;
        tuning->trigger_level = get_u16(buffer, index);
        index += 2;
        tuning->release_level = get_u16(buffer, index);
        index += 2;
        tuning->full_scale_level = get_u16(buffer, index);
        index += 2;
    }

    taiko_host_config_clamp(&parsed);
    *config = parsed;
    *command = requested == TAIKO_HOST_CONFIG_CMD_SAVE ? TAIKO_HOST_CONFIG_CMD_SAVE
                                                       : TAIKO_HOST_CONFIG_CMD_APPLY;
    return true;
}

void taiko_host_config_apply(const taiko_player_tuning_t *tuning,
                             taiko_hit_config_t *hit) {
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        hit->channel_gain_q8[channel] = tuning->channel_gain_q8[channel];
    }
    hit->noise_floor = tuning->noise_floor;
    hit->trigger_level = tuning->trigger_level;
    hit->release_level = tuning->release_level;
    hit->full_scale_level = tuning->full_scale_level;
}

void taiko_host_config_capture(const taiko_hit_config_t *hit,
                               taiko_player_tuning_t *tuning) {
    for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
        tuning->channel_gain_q8[channel] = hit->channel_gain_q8[channel];
    }
    tuning->noise_floor = hit->noise_floor;
    tuning->trigger_level = hit->trigger_level;
    tuning->release_level = hit->release_level;
    tuning->full_scale_level = hit->full_scale_level;
}
