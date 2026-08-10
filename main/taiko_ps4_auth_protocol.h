#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAIKO_PS4_AUTH_NONCE_SIZE 256U
#define TAIKO_PS4_AUTH_RESPONSE_SIZE 1064U
#define TAIKO_PS4_AUTH_CHUNK_SIZE 56U
#define TAIKO_PS4_AUTH_CHUNK_COUNT 19U
#define TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE 63U
#define TAIKO_PS4_AUTH_STATUS_PAYLOAD_SIZE 15U

typedef enum {
    TAIKO_PS4_AUTH_RECEIVING_NONCE = 0,
    TAIKO_PS4_AUTH_SIGN_PENDING,
    TAIKO_PS4_AUTH_SIGNING,
    TAIKO_PS4_AUTH_RESPONSE_READY,
} taiko_ps4_auth_state_t;

typedef enum {
    TAIKO_PS4_AUTH_NONCE_REJECTED = 0,
    TAIKO_PS4_AUTH_NONCE_ACCEPTED,
    TAIKO_PS4_AUTH_NONCE_READY_TO_SIGN,
} taiko_ps4_auth_nonce_result_t;

typedef struct {
    taiko_ps4_auth_state_t state;
    uint32_t generation;
    uint8_t nonce_id;
    uint8_t next_nonce_page;
    uint8_t next_response_chunk;
    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE];
    uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE];
} taiko_ps4_auth_protocol_t;

void taiko_ps4_auth_protocol_init(taiko_ps4_auth_protocol_t *protocol);
void taiko_ps4_auth_protocol_reset(taiko_ps4_auth_protocol_t *protocol);

taiko_ps4_auth_nonce_result_t taiko_ps4_auth_protocol_accept_nonce(
    taiko_ps4_auth_protocol_t *protocol,
    const uint8_t *payload,
    size_t payload_size);

bool taiko_ps4_auth_protocol_begin_signing(
    taiko_ps4_auth_protocol_t *protocol,
    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE],
    uint32_t *generation);
bool taiko_ps4_auth_protocol_finish_signing(
    taiko_ps4_auth_protocol_t *protocol,
    uint32_t generation,
    const uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE]);
void taiko_ps4_auth_protocol_fail_signing(
    taiko_ps4_auth_protocol_t *protocol, uint32_t generation);

size_t taiko_ps4_auth_protocol_build_signature_report(
    taiko_ps4_auth_protocol_t *protocol,
    uint8_t *payload,
    size_t payload_size);
size_t taiko_ps4_auth_protocol_build_status_report(
    const taiko_ps4_auth_protocol_t *protocol,
    uint8_t *payload,
    size_t payload_size);

uint32_t taiko_ps4_auth_protocol_crc32(const uint8_t *data, size_t length);
