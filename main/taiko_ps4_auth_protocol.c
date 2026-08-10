/*
 * PS4 authentication report framing follows the MIT-licensed Passing Link and
 * GP2040-CE implementations.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2020 Josh Gao
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity
 * SPDX-License-Identifier: MIT
 */

#include "taiko_ps4_auth_protocol.h"

#include <string.h>

#define PS4_SET_AUTH_REPORT_ID 0xf0U
#define PS4_GET_SIGNATURE_REPORT_ID 0xf1U
#define PS4_GET_STATUS_REPORT_ID 0xf2U
#define PS4_AUTH_NONCE_PAGE_COUNT 5U

_Static_assert(TAIKO_PS4_AUTH_CHUNK_SIZE * TAIKO_PS4_AUTH_CHUNK_COUNT ==
                   TAIKO_PS4_AUTH_RESPONSE_SIZE,
               "PS4 authentication response does not fill complete chunks");

static uint32_t read_u32_le(const uint8_t data[4]) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32_le(uint8_t data[4], uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

uint32_t taiko_ps4_auth_protocol_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void taiko_ps4_auth_protocol_init(taiko_ps4_auth_protocol_t *protocol) {
    if (protocol == NULL) {
        return;
    }
    memset(protocol, 0, sizeof(*protocol));
    protocol->state = TAIKO_PS4_AUTH_RECEIVING_NONCE;
    protocol->generation = 1;
}

void taiko_ps4_auth_protocol_reset(taiko_ps4_auth_protocol_t *protocol) {
    if (protocol == NULL) {
        return;
    }
    protocol->state = TAIKO_PS4_AUTH_RECEIVING_NONCE;
    protocol->generation++;
    protocol->nonce_id = 0;
    protocol->next_nonce_page = 0;
    protocol->next_response_chunk = 0;
}

static bool nonce_report_crc_valid(const uint8_t *payload) {
    uint8_t report[64] = {PS4_SET_AUTH_REPORT_ID};
    memcpy(&report[1], payload, TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE);
    return taiko_ps4_auth_protocol_crc32(report, 60) ==
           read_u32_le(&report[60]);
}

taiko_ps4_auth_nonce_result_t taiko_ps4_auth_protocol_accept_nonce(
    taiko_ps4_auth_protocol_t *protocol,
    const uint8_t *payload,
    size_t payload_size) {
    if (protocol == NULL || payload == NULL ||
        payload_size != TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE ||
        !nonce_report_crc_valid(payload)) {
        return TAIKO_PS4_AUTH_NONCE_REJECTED;
    }

    const uint8_t nonce_id = payload[0];
    const uint8_t nonce_page = payload[1];
    if (nonce_page >= PS4_AUTH_NONCE_PAGE_COUNT) {
        return TAIKO_PS4_AUTH_NONCE_REJECTED;
    }

    if (nonce_page == 0) {
        // A new page zero supersedes an interrupted or stale challenge.
        protocol->generation++;
        protocol->state = TAIKO_PS4_AUTH_RECEIVING_NONCE;
        protocol->nonce_id = nonce_id;
        protocol->next_nonce_page = 1;
        protocol->next_response_chunk = 0;
        memcpy(protocol->nonce, &payload[3], TAIKO_PS4_AUTH_CHUNK_SIZE);
        return TAIKO_PS4_AUTH_NONCE_ACCEPTED;
    }

    if (protocol->state != TAIKO_PS4_AUTH_RECEIVING_NONCE ||
        nonce_id != protocol->nonce_id ||
        nonce_page != protocol->next_nonce_page) {
        taiko_ps4_auth_protocol_reset(protocol);
        return TAIKO_PS4_AUTH_NONCE_REJECTED;
    }

    const size_t nonce_offset = (size_t)nonce_page * TAIKO_PS4_AUTH_CHUNK_SIZE;
    const size_t nonce_bytes =
        nonce_page == PS4_AUTH_NONCE_PAGE_COUNT - 1
            ? TAIKO_PS4_AUTH_NONCE_SIZE - nonce_offset
            : TAIKO_PS4_AUTH_CHUNK_SIZE;
    memcpy(&protocol->nonce[nonce_offset], &payload[3], nonce_bytes);

    if (nonce_page == PS4_AUTH_NONCE_PAGE_COUNT - 1) {
        protocol->state = TAIKO_PS4_AUTH_SIGN_PENDING;
        protocol->next_nonce_page = 0;
        return TAIKO_PS4_AUTH_NONCE_READY_TO_SIGN;
    }

    protocol->next_nonce_page++;
    return TAIKO_PS4_AUTH_NONCE_ACCEPTED;
}

bool taiko_ps4_auth_protocol_begin_signing(
    taiko_ps4_auth_protocol_t *protocol,
    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE],
    uint32_t *generation) {
    if (protocol == NULL || nonce == NULL || generation == NULL ||
        protocol->state != TAIKO_PS4_AUTH_SIGN_PENDING) {
        return false;
    }

    protocol->state = TAIKO_PS4_AUTH_SIGNING;
    memcpy(nonce, protocol->nonce, TAIKO_PS4_AUTH_NONCE_SIZE);
    *generation = protocol->generation;
    return true;
}

bool taiko_ps4_auth_protocol_finish_signing(
    taiko_ps4_auth_protocol_t *protocol,
    uint32_t generation,
    const uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE]) {
    if (protocol == NULL || response == NULL ||
        protocol->state != TAIKO_PS4_AUTH_SIGNING ||
        protocol->generation != generation) {
        return false;
    }

    memcpy(protocol->response, response, TAIKO_PS4_AUTH_RESPONSE_SIZE);
    protocol->state = TAIKO_PS4_AUTH_RESPONSE_READY;
    protocol->next_response_chunk = 0;
    return true;
}

void taiko_ps4_auth_protocol_fail_signing(
    taiko_ps4_auth_protocol_t *protocol, uint32_t generation) {
    if (protocol != NULL && protocol->state == TAIKO_PS4_AUTH_SIGNING &&
        protocol->generation == generation) {
        taiko_ps4_auth_protocol_reset(protocol);
    }
}

size_t taiko_ps4_auth_protocol_build_signature_report(
    taiko_ps4_auth_protocol_t *protocol,
    uint8_t *payload,
    size_t payload_size) {
    if (protocol == NULL || payload == NULL ||
        payload_size < TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE ||
        protocol->state != TAIKO_PS4_AUTH_RESPONSE_READY ||
        protocol->next_response_chunk >= TAIKO_PS4_AUTH_CHUNK_COUNT) {
        return 0;
    }

    uint8_t report[64] = {
        PS4_GET_SIGNATURE_REPORT_ID,
        protocol->nonce_id,
        protocol->next_response_chunk,
        0,
    };
    const size_t response_offset =
        (size_t)protocol->next_response_chunk * TAIKO_PS4_AUTH_CHUNK_SIZE;
    memcpy(&report[4], &protocol->response[response_offset],
           TAIKO_PS4_AUTH_CHUNK_SIZE);
    write_u32_le(&report[60], taiko_ps4_auth_protocol_crc32(report, 60));
    memcpy(payload, &report[1], TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE);

    protocol->next_response_chunk++;
    if (protocol->next_response_chunk == TAIKO_PS4_AUTH_CHUNK_COUNT) {
        protocol->state = TAIKO_PS4_AUTH_RECEIVING_NONCE;
        protocol->next_response_chunk = 0;
        protocol->next_nonce_page = 0;
    }
    return TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE;
}

size_t taiko_ps4_auth_protocol_build_status_report(
    const taiko_ps4_auth_protocol_t *protocol,
    uint8_t *payload,
    size_t payload_size) {
    if (protocol == NULL || payload == NULL ||
        payload_size < TAIKO_PS4_AUTH_STATUS_PAYLOAD_SIZE) {
        return 0;
    }

    uint8_t report[16] = {
        PS4_GET_STATUS_REPORT_ID,
        protocol->nonce_id,
        protocol->state == TAIKO_PS4_AUTH_RESPONSE_READY ? 0U : 0x10U,
    };
    write_u32_le(&report[12], taiko_ps4_auth_protocol_crc32(report, 12));
    memcpy(payload, &report[1], TAIKO_PS4_AUTH_STATUS_PAYLOAD_SIZE);
    return TAIKO_PS4_AUTH_STATUS_PAYLOAD_SIZE;
}
