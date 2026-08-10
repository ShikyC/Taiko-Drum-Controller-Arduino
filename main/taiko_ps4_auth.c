/*
 * Credential-backed PS4 challenge handling follows the protocol implemented
 * by the MIT-licensed Passing Link and GP2040-CE projects.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2020 Josh Gao
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity
 * SPDX-License-Identifier: MIT
 */

#include "taiko_ps4_auth.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "taiko_ps4_auth_protocol.h"

#ifdef TAIKO_PS4_AUTH_EMBEDDED
#include "bootloader_random.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#endif

#define PS4_AUTH_TASK_STACK_SIZE 12288U
#define PS4_AUTH_TASK_PRIORITY 2U

#ifdef CONFIG_FREERTOS_UNICORE
#define PS4_AUTH_TASK_CORE 0
#else
#define PS4_AUTH_TASK_CORE 1
#endif

static taiko_ps4_auth_protocol_t s_protocol;
static portMUX_TYPE s_protocol_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static bool s_available;

#ifdef TAIKO_PS4_AUTH_EMBEDDED

static const char *TAG = "taiko_ps4_auth";
static TaskHandle_t s_auth_task_handle;

extern const uint8_t _binary_taiko_ps4_private_key_start[];
extern const uint8_t _binary_taiko_ps4_private_key_end[];
extern const uint8_t _binary_taiko_ps4_serial_text_start[];
extern const uint8_t _binary_taiko_ps4_serial_text_end[];
extern const uint8_t _binary_taiko_ps4_signature_start[];
extern const uint8_t _binary_taiko_ps4_signature_end[];

static mbedtls_pk_context s_private_key;
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool s_crypto_initialized;
static uint8_t s_serial[16];
static uint8_t s_signature[256];
static uint8_t s_rsa_modulus[256];
static uint8_t s_rsa_exponent[256];

typedef struct {
    TaskHandle_t caller;
    esp_err_t result;
} auth_init_context_t;

static bool ascii_space(uint8_t value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int hex_nibble(uint8_t value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static esp_err_t load_serial(void) {
    const uint8_t *begin = _binary_taiko_ps4_serial_text_start;
    const uint8_t *end = _binary_taiko_ps4_serial_text_end;
    if (end > begin && end[-1] == 0) {
        end--;
    }
    while (begin < end && ascii_space(*begin)) {
        begin++;
    }
    while (end > begin && ascii_space(end[-1])) {
        end--;
    }
    if ((size_t)(end - begin) != 16U) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(s_serial, 0, sizeof(s_serial));
    for (size_t index = 0; index < 8; ++index) {
        const int high = hex_nibble(begin[index * 2]);
        const int low = hex_nibble(begin[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        // GP2040-CE accepts a 16-character hexadecimal serial file and
        // left-pads it to the 16-byte value sent in the authentication block.
        s_serial[8 + index] = (uint8_t)((high << 4) | low);
    }
    return ESP_OK;
}

static void free_crypto(void) {
    if (!s_crypto_initialized) {
        return;
    }
    mbedtls_pk_free(&s_private_key);
    mbedtls_ctr_drbg_free(&s_ctr_drbg);
    mbedtls_entropy_free(&s_entropy);
    s_crypto_initialized = false;
}

static esp_err_t load_embedded_credentials(void) {
    const size_t private_key_size =
        (size_t)(_binary_taiko_ps4_private_key_end -
                 _binary_taiko_ps4_private_key_start);
    const size_t signature_size =
        (size_t)(_binary_taiko_ps4_signature_end -
                 _binary_taiko_ps4_signature_start);
    if (private_key_size < 2U ||
        _binary_taiko_ps4_private_key_start[private_key_size - 1] != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (signature_size != sizeof(s_signature)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = load_serial();
    if (result != ESP_OK) {
        return result;
    }
    memcpy(s_signature, _binary_taiko_ps4_signature_start,
           sizeof(s_signature));

    mbedtls_pk_init(&s_private_key);
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);
    s_crypto_initialized = true;

    static const uint8_t personalization[] = "taiko-ps4-auth";
    // The ADC entropy source is safe here because app_main waits for this task
    // and has not initialized continuous ADC sampling yet. The seeded DRBG is
    // retained for all later PSS salt and RSA blinding requests.
    bootloader_random_enable();
    const int seed_result = mbedtls_ctr_drbg_seed(
        &s_ctr_drbg, mbedtls_entropy_func, &s_entropy, personalization,
        sizeof(personalization) - 1U);
    bootloader_random_disable();
    if (seed_result != 0) {
        ESP_LOGE(TAG, "DRBG initialization failed: -0x%04x", -seed_result);
        free_crypto();
        return ESP_FAIL;
    }

    const int parse_result = mbedtls_pk_parse_key(
        &s_private_key, _binary_taiko_ps4_private_key_start, private_key_size,
        NULL, 0, mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (parse_result != 0) {
        ESP_LOGE(TAG, "private-key parsing failed: -0x%04x", -parse_result);
        free_crypto();
        return ESP_ERR_INVALID_ARG;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_private_key);
    if (rsa == NULL || mbedtls_rsa_get_bitlen(rsa) != 2048U ||
        mbedtls_rsa_check_privkey(rsa) != 0 ||
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21,
                                MBEDTLS_MD_SHA256) != 0 ||
        mbedtls_rsa_export_raw(rsa, s_rsa_modulus, sizeof(s_rsa_modulus),
                               NULL, 0, NULL, 0, NULL, 0, s_rsa_exponent,
                               sizeof(s_rsa_exponent)) != 0) {
        ESP_LOGE(TAG, "credential key must be a valid 2048-bit RSA private key");
        free_crypto();
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t build_auth_response(
    const uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE],
    uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE]) {
    uint8_t digest[32];
    if (mbedtls_sha256(nonce, TAIKO_PS4_AUTH_NONCE_SIZE, digest, false) != 0) {
        return ESP_FAIL;
    }

    memset(response, 0, TAIKO_PS4_AUTH_RESPONSE_SIZE);
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_private_key);
    const int sign_result = mbedtls_rsa_rsassa_pss_sign(
        rsa, mbedtls_ctr_drbg_random, &s_ctr_drbg, MBEDTLS_MD_SHA256,
        sizeof(digest), digest, response);
    if (sign_result != 0) {
        ESP_LOGE(TAG, "nonce signing failed: -0x%04x", -sign_result);
        return ESP_FAIL;
    }

    size_t offset = 256;
    memcpy(&response[offset], s_serial, sizeof(s_serial));
    offset += sizeof(s_serial);
    memcpy(&response[offset], s_rsa_modulus, sizeof(s_rsa_modulus));
    offset += sizeof(s_rsa_modulus);
    memcpy(&response[offset], s_rsa_exponent, sizeof(s_rsa_exponent));
    offset += sizeof(s_rsa_exponent);
    memcpy(&response[offset], s_signature, sizeof(s_signature));
    offset += sizeof(s_signature);
    // The final 24 bytes remain zero padding.
    return offset + 24U == TAIKO_PS4_AUTH_RESPONSE_SIZE ? ESP_OK : ESP_FAIL;
}

static void auth_task(void *argument) {
    auth_init_context_t *init = (auth_init_context_t *)argument;
    const esp_err_t init_result = load_embedded_credentials();
    if (init_result == ESP_OK) {
        __atomic_store_n(&s_available, true, __ATOMIC_RELEASE);
        ESP_LOGI(TAG, "credential-backed PS4 authentication enabled");
    }
    init->result = init_result;
    xTaskNotifyGive(init->caller);

    if (init_result != ESP_OK) {
        __atomic_store_n(&s_auth_task_handle, NULL, __ATOMIC_RELEASE);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (true) {
            uint8_t nonce[TAIKO_PS4_AUTH_NONCE_SIZE];
            uint8_t response[TAIKO_PS4_AUTH_RESPONSE_SIZE];
            uint32_t generation = 0;

            portENTER_CRITICAL(&s_protocol_lock);
            const bool should_sign = taiko_ps4_auth_protocol_begin_signing(
                &s_protocol, nonce, &generation);
            portEXIT_CRITICAL(&s_protocol_lock);
            if (!should_sign) {
                break;
            }

            const esp_err_t sign_result = build_auth_response(nonce, response);
            portENTER_CRITICAL(&s_protocol_lock);
            if (sign_result == ESP_OK) {
                (void)taiko_ps4_auth_protocol_finish_signing(
                    &s_protocol, generation, response);
            } else {
                taiko_ps4_auth_protocol_fail_signing(&s_protocol, generation);
            }
            portEXIT_CRITICAL(&s_protocol_lock);
        }
    }
}

#endif  // TAIKO_PS4_AUTH_EMBEDDED

esp_err_t taiko_ps4_auth_init(void) {
    if (__atomic_exchange_n(&s_initialized, true, __ATOMIC_ACQ_REL)) {
        return ESP_ERR_INVALID_STATE;
    }
    taiko_ps4_auth_protocol_init(&s_protocol);

#ifndef TAIKO_PS4_AUTH_EMBEDDED
    return ESP_ERR_NOT_SUPPORTED;
#else
    auth_init_context_t init = {
        .caller = xTaskGetCurrentTaskHandle(),
        .result = ESP_FAIL,
    };
    if (xTaskCreatePinnedToCore(
            auth_task, "ps4_auth", PS4_AUTH_TASK_STACK_SIZE, &init,
            PS4_AUTH_TASK_PRIORITY, &s_auth_task_handle,
            PS4_AUTH_TASK_CORE) != pdPASS) {
        s_auth_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return init.result;
#endif
}

bool taiko_ps4_auth_available(void) {
    return __atomic_load_n(&s_available, __ATOMIC_ACQUIRE);
}

bool taiko_ps4_auth_set_nonce(const uint8_t *payload, size_t payload_size) {
    if (!taiko_ps4_auth_available()) {
        return false;
    }

    portENTER_CRITICAL(&s_protocol_lock);
    const taiko_ps4_auth_nonce_result_t result =
        taiko_ps4_auth_protocol_accept_nonce(&s_protocol, payload,
                                             payload_size);
    portEXIT_CRITICAL(&s_protocol_lock);

#ifdef TAIKO_PS4_AUTH_EMBEDDED
    if (result == TAIKO_PS4_AUTH_NONCE_READY_TO_SIGN) {
        const TaskHandle_t task = __atomic_load_n(
            &s_auth_task_handle, __ATOMIC_ACQUIRE);
        if (task != NULL) {
            xTaskNotifyGive(task);
        }
    }
#endif
    return result != TAIKO_PS4_AUTH_NONCE_REJECTED;
}

size_t taiko_ps4_auth_get_signature(uint8_t *payload, size_t payload_size) {
    if (!taiko_ps4_auth_available()) {
        return 0;
    }
    portENTER_CRITICAL(&s_protocol_lock);
    const size_t result = taiko_ps4_auth_protocol_build_signature_report(
        &s_protocol, payload, payload_size);
    portEXIT_CRITICAL(&s_protocol_lock);
    return result;
}

size_t taiko_ps4_auth_get_status(uint8_t *payload, size_t payload_size) {
    if (!taiko_ps4_auth_available()) {
        return 0;
    }
    portENTER_CRITICAL(&s_protocol_lock);
    const size_t result = taiko_ps4_auth_protocol_build_status_report(
        &s_protocol, payload, payload_size);
    portEXIT_CRITICAL(&s_protocol_lock);
    return result;
}

void taiko_ps4_auth_reset(void) {
    if (!taiko_ps4_auth_available()) {
        return;
    }
    portENTER_CRITICAL(&s_protocol_lock);
    taiko_ps4_auth_protocol_reset(&s_protocol);
    portEXIT_CRITICAL(&s_protocol_lock);
}
