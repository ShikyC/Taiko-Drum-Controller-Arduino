#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "taiko_hit_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-writable tuning, carried over a vendor-defined HID feature report on
 * the Arcade interface. Feature items add no Input items to the descriptor, so
 * the gamepad's axes -- and every host's mapping of them -- are unchanged.
 *
 * Sensitivity lives here rather than in the firmware because it is a property
 * of the drum in front of you, not of the board. DIP1 and DIP2 still choose
 * the drum type, which is a waveform-shape fact the board can read off a
 * switch; everything about amplitude is the host's. */

#define TAIKO_HOST_CONFIG_REPORT_ID 0x10
#define TAIKO_HOST_CONFIG_VERSION 1
#define TAIKO_HOST_CONFIG_PLAYERS 2
/* Four header bytes plus eight little-endian u16 per player. */
#define TAIKO_HOST_CONFIG_REPORT_SIZE 36

/* Byte 1 of a written report. */
enum {
    TAIKO_HOST_CONFIG_CMD_APPLY = 0, /* apply live, do not persist */
    TAIKO_HOST_CONFIG_CMD_SAVE = 1,  /* apply live and persist */
    TAIKO_HOST_CONFIG_CMD_RESET = 2, /* ignore payload, restore defaults */
};

/* Byte 2 of a read report: what the board is actually running. */
enum {
    TAIKO_HOST_CONFIG_STATUS_P1_LONG_TAIL = 1U << 0,
    TAIKO_HOST_CONFIG_STATUS_P2_LONG_TAIL = 1U << 1,
    TAIKO_HOST_CONFIG_STATUS_STORED = 1U << 2,
    TAIKO_HOST_CONFIG_STATUS_TWO_PLAYERS = 1U << 3,
};

/* Accepted ranges. A host is not trusted to send something that would stop the
 * detector working, so every field is clamped on the way in. */
#define TAIKO_GAIN_Q8_MIN 16U   /* 0.0625x */
#define TAIKO_GAIN_Q8_MAX 4096U /* 16x */
#define TAIKO_NOISE_FLOOR_MAX 255U
#define TAIKO_FULL_SCALE_MIN 64U
#define TAIKO_FULL_SCALE_MAX 4095U /* 12-bit ADC span */
#define TAIKO_TRIGGER_MIN 8U
#define TAIKO_RELEASE_MIN 4U

typedef struct {
    uint16_t channel_gain_q8[TAIKO_CHANNELS_PER_PLAYER];
    uint16_t noise_floor;
    uint16_t trigger_level;
    uint16_t release_level;
    uint16_t full_scale_level;
} taiko_player_tuning_t;

typedef struct {
    taiko_player_tuning_t player[TAIKO_HOST_CONFIG_PLAYERS];
} taiko_host_config_t;

/** Uniform shipping values, taken from the standard drum profile. */
void taiko_host_config_defaults(taiko_host_config_t *config);

/** Force every field into range, keeping release < trigger < full scale. */
void taiko_host_config_clamp(taiko_host_config_t *config);

/** Write the report a host reads back. Returns the byte count, 0 if it cannot fit. */
size_t taiko_host_config_serialize(const taiko_host_config_t *config,
                                   uint8_t status, uint8_t *buffer, size_t size);

/**
 * Parse a written report. Returns false, leaving both outputs untouched, for a
 * short buffer or an unrecognised schema version. The parsed config is always
 * clamped, so a caller never sees out-of-range values.
 */
bool taiko_host_config_deserialize(const uint8_t *buffer, size_t size,
                                   taiko_host_config_t *config, uint8_t *command);

/** Copy one player's tuning into a live detector config. */
void taiko_host_config_apply(const taiko_player_tuning_t *tuning,
                             taiko_hit_config_t *hit);

/** Read one player's tuning back out of a detector config. */
void taiko_host_config_capture(const taiko_hit_config_t *hit,
                               taiko_player_tuning_t *tuning);

#ifdef __cplusplus
}
#endif
