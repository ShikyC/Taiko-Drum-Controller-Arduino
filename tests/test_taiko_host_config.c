#include "taiko_host_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_round_trip(void) {
    taiko_host_config_t original;
    taiko_host_config_defaults(&original);
    original.player[0].channel_gain_q8[1] = 512;  /* 2x on left Ka */
    original.player[1].trigger_level = 96;
    original.player[1].full_scale_level = 1800;

    uint8_t buffer[TAIKO_HOST_CONFIG_REPORT_SIZE];
    assert(taiko_host_config_serialize(&original, 0, buffer, sizeof(buffer)) ==
           TAIKO_HOST_CONFIG_REPORT_SIZE);

    taiko_host_config_t parsed;
    uint8_t command = 0xFF;
    memset(&parsed, 0, sizeof(parsed));
    assert(taiko_host_config_deserialize(buffer, sizeof(buffer), &parsed, &command));
    assert(command == TAIKO_HOST_CONFIG_CMD_APPLY);
    assert(memcmp(&original, &parsed, sizeof(parsed)) == 0);
}

static void test_short_and_versioned_reports_are_rejected(void) {
    taiko_host_config_t config;
    taiko_host_config_defaults(&config);
    uint8_t buffer[TAIKO_HOST_CONFIG_REPORT_SIZE];
    (void)taiko_host_config_serialize(&config, 0, buffer, sizeof(buffer));

    taiko_host_config_t parsed;
    uint8_t command = 0;
    assert(!taiko_host_config_deserialize(buffer, sizeof(buffer) - 1, &parsed, &command));

    buffer[0] = TAIKO_HOST_CONFIG_VERSION + 1;
    assert(!taiko_host_config_deserialize(buffer, sizeof(buffer), &parsed, &command));
}

/* A host is not trusted: nothing it sends may leave the detector in a state
 * where release >= trigger >= full scale, which would stop it triggering. */
static void test_hostile_values_are_clamped(void) {
    taiko_host_config_t config;
    taiko_host_config_defaults(&config);
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        config.player[player].full_scale_level = 0;
        config.player[player].trigger_level = 60000;
        config.player[player].release_level = 60000;
        config.player[player].noise_floor = 60000;
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            config.player[player].channel_gain_q8[channel] = 0;
        }
    }
    taiko_host_config_clamp(&config);

    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        const taiko_player_tuning_t *t = &config.player[player];
        assert(t->full_scale_level >= TAIKO_FULL_SCALE_MIN);
        assert(t->full_scale_level <= TAIKO_FULL_SCALE_MAX);
        assert(t->trigger_level < t->full_scale_level);
        assert(t->release_level < t->trigger_level);
        assert(t->noise_floor <= TAIKO_NOISE_FLOOR_MAX);
        for (size_t channel = 0; channel < TAIKO_CHANNELS_PER_PLAYER; ++channel) {
            assert(t->channel_gain_q8[channel] >= TAIKO_GAIN_Q8_MIN);
            assert(t->channel_gain_q8[channel] <= TAIKO_GAIN_Q8_MAX);
        }
    }
}

static void test_reset_command_ignores_payload(void) {
    taiko_host_config_t config;
    taiko_host_config_defaults(&config);
    for (size_t player = 0; player < TAIKO_HOST_CONFIG_PLAYERS; ++player) {
        config.player[player].trigger_level = 999;
    }
    uint8_t buffer[TAIKO_HOST_CONFIG_REPORT_SIZE];
    (void)taiko_host_config_serialize(&config, 0, buffer, sizeof(buffer));
    buffer[1] = TAIKO_HOST_CONFIG_CMD_RESET;

    taiko_host_config_t parsed;
    uint8_t command = 0;
    assert(taiko_host_config_deserialize(buffer, sizeof(buffer), &parsed, &command));
    assert(command == TAIKO_HOST_CONFIG_CMD_RESET);

    taiko_host_config_t defaults;
    taiko_host_config_defaults(&defaults);
    assert(memcmp(&parsed, &defaults, sizeof(defaults)) == 0);
}

/* Applying and reading back must be lossless, or the page would show something
 * other than what the board is running. */
static void test_apply_capture_is_lossless(void) {
    taiko_host_config_t config;
    taiko_host_config_defaults(&config);
    config.player[0].channel_gain_q8[3] = 384;
    config.player[0].noise_floor = 22;
    config.player[0].trigger_level = 55;
    config.player[0].release_level = 30;
    config.player[0].full_scale_level = 1500;

    taiko_hit_config_t hit = taiko_hit_default_config();
    taiko_host_config_apply(&config.player[0], &hit);

    taiko_player_tuning_t read_back;
    taiko_host_config_capture(&hit, &read_back);
    assert(memcmp(&config.player[0], &read_back, sizeof(read_back)) == 0);

    /* Drum-type settings are not the host's and must survive an apply. */
    const taiko_hit_config_t long_tail = taiko_hit_long_tail_config();
    taiko_hit_config_t tuned = long_tail;
    taiko_host_config_apply(&config.player[0], &tuned);
    assert(tuned.integration_samples == long_tail.integration_samples);
    assert(tuned.refractory_samples == long_tail.refractory_samples);
    assert(tuned.crosstalk_mask_samples == long_tail.crosstalk_mask_samples);
    assert(tuned.output_hold_samples == long_tail.output_hold_samples);
}

int main(void) {
    test_round_trip();
    test_short_and_versioned_reports_are_rejected();
    test_hostile_values_are_clamped();
    test_reset_command_ignores_payload();
    test_apply_capture_is_lossless();
    puts("taiko_host_config tests passed");
    return 0;
}
