#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "taiko_ps4_auth_protocol.h"

static void write_u32_le(uint8_t data[4], uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32_le(const uint8_t data[4]) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void make_nonce_payload(
    uint8_t nonce_id,
    uint8_t page,
    const uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE],
    uint8_t payload[TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE]) {
    uint8_t report[64] = {0xf0, nonce_id, page, 0};
    const size_t offset = (size_t)page * TAIKO_PS4_AUTH_CHUNK_SIZE;
    const size_t count = page < 4   ? TAIKO_PS4_AUTH_CHUNK_SIZE
                         : page == 4 ? TAIKO_PS4_AUTH_NONCE_SIZE - offset
                                     : 0;
    if (count != 0) {
        memcpy(&report[4], &nonce[offset], count);
    }
    write_u32_le(&report[60], taiko_ps4_auth_protocol_crc32(report, 60));
    memcpy(payload, &report[1], TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE);
}

static void assert_payload_crc(uint8_t report_id, const uint8_t *payload,
                               size_t full_size) {
    uint8_t report[64] = {report_id};
    assert(full_size <= sizeof(report));
    memcpy(&report[1], payload, full_size - 1);
    assert(read_u32_le(&report[full_size - 4]) ==
           taiko_ps4_auth_protocol_crc32(report, full_size - 4));
}

static void receive_nonce(taiko_ps4_auth_protocol_t *protocol,
                          uint8_t nonce_id,
                          const uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE]) {
    for (uint8_t page = 0; page < 5; ++page) {
        uint8_t payload[TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE];
        make_nonce_payload(nonce_id, page, nonce, payload);
        const taiko_ps4_auth_nonce_result_t expected =
            page == 4 ? TAIKO_PS4_AUTH_NONCE_READY_TO_SIGN
                      : TAIKO_PS4_AUTH_NONCE_ACCEPTED;
        assert(taiko_ps4_auth_protocol_accept_nonce(
                   protocol, payload, sizeof(payload)) == expected);
    }
}

static void test_complete_challenge(void) {
    taiko_ps4_auth_protocol_t protocol;
    taiko_ps4_auth_protocol_init(&protocol);

    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE];
    for (size_t index = 0; index < sizeof(nonce); ++index) {
        nonce[index] = (uint8_t)(index ^ 0xa5U);
    }
    receive_nonce(&protocol, 0x37, nonce);
    assert(protocol.state == TAIKO_PS4_AUTH_SIGN_PENDING);

    uint8_t status[TAIKO_PS4_AUTH_STATUS_PAYLOAD_SIZE];
    assert(taiko_ps4_auth_protocol_build_status_report(
               &protocol, status, sizeof(status)) == sizeof(status));
    assert(status[0] == 0x37);
    assert(status[1] == 0x10);
    assert_payload_crc(0xf2, status, 16);

    uint8_t nonce_copy[TAIKO_PS4_AUTH_NONCE_SIZE];
    uint32_t generation = 0;
    assert(taiko_ps4_auth_protocol_begin_signing(
        &protocol, nonce_copy, &generation));
    assert(memcmp(nonce, nonce_copy, sizeof(nonce)) == 0);
    assert(protocol.state == TAIKO_PS4_AUTH_SIGNING);

    uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE];
    for (size_t index = 0; index < sizeof(response); ++index) {
        response[index] = (uint8_t)(index * 29U + 7U);
    }
    assert(taiko_ps4_auth_protocol_finish_signing(
        &protocol, generation, response));
    assert(protocol.state == TAIKO_PS4_AUTH_RESPONSE_READY);

    assert(taiko_ps4_auth_protocol_build_status_report(
               &protocol, status, sizeof(status)) == sizeof(status));
    assert(status[0] == 0x37);
    assert(status[1] == 0);
    assert_payload_crc(0xf2, status, 16);

    uint8_t reconstructed[TAIKO_PS4_AUTH_RESPONSE_SIZE] = {0};
    for (uint8_t chunk = 0; chunk < TAIKO_PS4_AUTH_CHUNK_COUNT; ++chunk) {
        uint8_t payload[TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE];
        assert(taiko_ps4_auth_protocol_build_signature_report(
                   &protocol, payload, sizeof(payload)) == sizeof(payload));
        assert(payload[0] == 0x37);
        assert(payload[1] == chunk);
        assert(payload[2] == 0);
        assert_payload_crc(0xf1, payload, 64);
        memcpy(&reconstructed[(size_t)chunk * TAIKO_PS4_AUTH_CHUNK_SIZE],
               &payload[3], TAIKO_PS4_AUTH_CHUNK_SIZE);
    }
    assert(memcmp(response, reconstructed, sizeof(response)) == 0);
    assert(protocol.state == TAIKO_PS4_AUTH_RECEIVING_NONCE);
    assert(taiko_ps4_auth_protocol_build_signature_report(
               &protocol, reconstructed,
               TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE) == 0);
}

static void test_invalid_nonce_packets(void) {
    taiko_ps4_auth_protocol_t protocol;
    taiko_ps4_auth_protocol_init(&protocol);
    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE] = {0};
    uint8_t payload[TAIKO_PS4_AUTH_FEATURE_PAYLOAD_SIZE];

    make_nonce_payload(3, 0, nonce, payload);
    payload[10] ^= 1U;
    assert(taiko_ps4_auth_protocol_accept_nonce(
               &protocol, payload, sizeof(payload)) ==
           TAIKO_PS4_AUTH_NONCE_REJECTED);
    assert(protocol.next_nonce_page == 0);

    make_nonce_payload(3, 0, nonce, payload);
    assert(taiko_ps4_auth_protocol_accept_nonce(
               &protocol, payload, sizeof(payload)) ==
           TAIKO_PS4_AUTH_NONCE_ACCEPTED);
    make_nonce_payload(3, 2, nonce, payload);
    assert(taiko_ps4_auth_protocol_accept_nonce(
               &protocol, payload, sizeof(payload)) ==
           TAIKO_PS4_AUTH_NONCE_REJECTED);
    assert(protocol.next_nonce_page == 0);

    make_nonce_payload(3, 5, nonce, payload);
    assert(taiko_ps4_auth_protocol_accept_nonce(
               &protocol, payload, sizeof(payload)) ==
           TAIKO_PS4_AUTH_NONCE_REJECTED);
}

static void test_stale_signature_is_discarded(void) {
    taiko_ps4_auth_protocol_t protocol;
    taiko_ps4_auth_protocol_init(&protocol);
    uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE] = {0};
    receive_nonce(&protocol, 9, nonce);

    uint8_t nonce_copy[TAIKO_PS4_AUTH_NONCE_SIZE];
    uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE] = {0};
    uint32_t generation = 0;
    assert(taiko_ps4_auth_protocol_begin_signing(
        &protocol, nonce_copy, &generation));
    taiko_ps4_auth_protocol_reset(&protocol);
    assert(!taiko_ps4_auth_protocol_finish_signing(
        &protocol, generation, response));
    assert(protocol.state == TAIKO_PS4_AUTH_RECEIVING_NONCE);
}

int main(void) {
    test_complete_challenge();
    test_invalid_nonce_packets();
    test_stale_signature_is_discarded();
    puts("taiko PS4 authentication protocol tests passed");
    return 0;
}
