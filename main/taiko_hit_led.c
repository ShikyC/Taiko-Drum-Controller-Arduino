#include "taiko_hit_led.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HIT_LED_GPIO GPIO_NUM_38
#define HIT_LED_COUNT 8U
#define HIT_LED_HOLD_MS 120U
#define HIT_LED_CHANNEL_LEVEL 96U
#define HIT_LED_RMT_RESOLUTION_HZ 10000000U
#define HIT_LED_RMT_SYMBOLS 64U
#define HIT_LED_TX_TIMEOUT_MS 10
#define HIT_LED_TASK_STACK_SIZE 3072U
#define HIT_LED_TASK_PRIORITY 1U

#ifdef CONFIG_FREERTOS_UNICORE
#define HIT_LED_TASK_CORE 0
#else
#define HIT_LED_TASK_CORE 1
#endif

#define HIT_LED_EVENT_MASK ((1U << HIT_LED_COUNT) - 1U)

static const char *TAG = "taiko_hit_led";
static TaskHandle_t s_hit_led_task_handle;

enum {
    LED_OFFSET_LEFT_KA = 0,
    LED_OFFSET_LEFT_DON,
    LED_OFFSET_RIGHT_DON,
    LED_OFFSET_RIGHT_KA,
};

// Translate detector zone order (LD, LK, RD, RK) into the physical chain
// order (LK, LD, RD, RK) used for each player.
static const uint8_t kLedOffsetByZone[TAIKO_CHANNELS_PER_PLAYER] = {
    [TAIKO_ZONE_LEFT_DON] = LED_OFFSET_LEFT_DON,
    [TAIKO_ZONE_LEFT_KA] = LED_OFFSET_LEFT_KA,
    [TAIKO_ZONE_RIGHT_DON] = LED_OFFSET_RIGHT_DON,
    [TAIKO_ZONE_RIGHT_KA] = LED_OFFSET_RIGHT_KA,
};

// The XL-1615RGBC-2812B-S identifies bits by their high time. These values
// keep the complete frame at its specified 800 kHz data rate.
static const DRAM_ATTR rmt_symbol_word_t kWs2812Zero = {
    .level0 = 1,
    .duration0 = 3,  // 0.3 us high
    .level1 = 0,
    .duration1 = 9,  // 0.9 us low
};

static const DRAM_ATTR rmt_symbol_word_t kWs2812One = {
    .level0 = 1,
    .duration0 = 9,  // 0.9 us high
    .level1 = 0,
    .duration1 = 3,  // 0.3 us low
};

static const DRAM_ATTR rmt_symbol_word_t kWs2812Reset = {
    .level0 = 0,
    .duration0 = 1500,  // 150 us low
    .level1 = 0,
    .duration1 = 1500,  // 150 us low; 300 us total reset
};

static size_t RMT_ENCODER_FUNC_ATTR encode_led_frame(
    const void *data,
    size_t data_size,
    size_t symbols_written,
    size_t symbols_free,
    rmt_symbol_word_t *symbols,
    bool *done,
    void *argument) {
    (void)argument;

    // A byte needs eight symbols. Waiting for the same space before appending
    // the reset symbol keeps the callback state derivable from symbols_written.
    if (symbols_free < 8) {
        return 0;
    }

    const size_t byte_index = symbols_written / 8;
    if (byte_index < data_size) {
        const uint8_t byte = ((const uint8_t *)data)[byte_index];
        for (size_t bit = 0; bit < 8; ++bit) {
            const uint8_t mask = (uint8_t)(0x80U >> bit);
            symbols[bit] =
                (byte & mask) != 0 ? kWs2812One : kWs2812Zero;
        }
        return 8;
    }

    // Use 300 us low so the latch interval exceeds the selected LED's
    // greater-than-200-us reset requirement with margin.
    symbols[0] = kWs2812Reset;
    *done = true;
    return 1;
}

static esp_err_t init_led_rmt(rmt_channel_handle_t *channel,
                              rmt_encoder_handle_t *encoder) {
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = HIT_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = HIT_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = HIT_LED_RMT_SYMBOLS,
        .trans_queue_depth = 1,
        .intr_priority = 0,
        .flags = {
            .init_level = 0,
        },
    };
    esp_err_t result = rmt_new_tx_channel(&channel_config, channel);
    if (result != ESP_OK) {
        return result;
    }

    const rmt_simple_encoder_config_t encoder_config = {
        .callback = encode_led_frame,
        .min_chunk_size = 8,
    };
    result = rmt_new_simple_encoder(&encoder_config, encoder);
    if (result != ESP_OK) {
        rmt_del_channel(*channel);
        *channel = NULL;
        return result;
    }

    result = rmt_enable(*channel);
    if (result != ESP_OK) {
        rmt_del_encoder(*encoder);
        rmt_del_channel(*channel);
        *encoder = NULL;
        *channel = NULL;
    }
    return result;
}

static bool led_is_don(size_t led) {
    const size_t offset = led % TAIKO_CHANNELS_PER_PLAYER;
    return offset == LED_OFFSET_LEFT_DON ||
           offset == LED_OFFSET_RIGHT_DON;
}

static esp_err_t write_led_frame(rmt_channel_handle_t channel,
                                 rmt_encoder_handle_t encoder,
                                 uint32_t active_channels) {
    // Each player uses physical order LK, LD, RD, RK. Each device consumes G,
    // R, B, most-significant bit first. Only one color component is ever set:
    // Don is clean red and Ka is clean blue, never a combined purple value.
    uint8_t grb[HIT_LED_COUNT * 3U] = {0};
    for (size_t led = 0; led < HIT_LED_COUNT; ++led) {
        if ((active_channels & (1U << led)) == 0) {
            continue;
        }
        if (led_is_don(led)) {
            grb[led * 3U + 1U] = HIT_LED_CHANNEL_LEVEL;
        } else {
            grb[led * 3U + 2U] = HIT_LED_CHANNEL_LEVEL;
        }
    }
    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
        },
    };

    esp_err_t result =
        rmt_transmit(channel, encoder, grb, sizeof(grb), &transmit_config);
    if (result == ESP_OK) {
        result =
            rmt_tx_wait_all_done(channel, HIT_LED_TX_TIMEOUT_MS);
    }
    return result;
}

static bool deadline_reached(TickType_t now, TickType_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static TickType_t next_deadline_wait(
    TickType_t now,
    uint32_t active_channels,
    const TickType_t active_until[HIT_LED_COUNT]) {
    TickType_t wait = portMAX_DELAY;
    for (size_t led = 0; led < HIT_LED_COUNT; ++led) {
        if ((active_channels & (1U << led)) == 0) {
            continue;
        }
        const TickType_t led_wait = active_until[led] - now;
        if (wait == portMAX_DELAY || led_wait < wait) {
            wait = led_wait;
        }
    }
    return wait == 0 ? 1 : wait;
}

static void hit_led_task(void *argument) {
    (void)argument;

    // Initializing from this core also keeps the RMT interrupt off the
    // core-0 ADC task.
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    const esp_err_t init_result = init_led_rmt(&channel, &encoder);
    if (init_result != ESP_OK) {
        ESP_LOGE(TAG, "indicator disabled: %s", esp_err_to_name(init_result));
        __atomic_store_n(
            &s_hit_led_task_handle, NULL, __ATOMIC_RELEASE);
        vTaskDelete(NULL);
        return;
    }

    TickType_t active_until[HIT_LED_COUNT] = {0};
    uint32_t active_channels = 0;
    uint32_t displayed_channels = UINT32_MAX;
    uint32_t events = 0;
    const TickType_t hold_ticks = pdMS_TO_TICKS(HIT_LED_HOLD_MS);

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        events &= HIT_LED_EVENT_MASK;
        for (size_t led = 0; led < HIT_LED_COUNT; ++led) {
            const uint32_t led_mask = 1U << led;
            if ((events & led_mask) != 0) {
                active_channels |= led_mask;
                active_until[led] = now + hold_ticks;
            }
        }

        for (size_t led = 0; led < HIT_LED_COUNT; ++led) {
            const uint32_t led_mask = 1U << led;
            if ((active_channels & led_mask) != 0 &&
                deadline_reached(now, active_until[led])) {
                active_channels &= ~led_mask;
            }
        }

        if (active_channels != displayed_channels) {
            const esp_err_t write_result =
                write_led_frame(channel, encoder, active_channels);
            if (write_result != ESP_OK) {
                ESP_LOGW(TAG, "channel update failed: %s",
                         esp_err_to_name(write_result));
            }
            displayed_channels = active_channels;
        }

        const TickType_t wait =
            next_deadline_wait(now, active_channels, active_until);
        events = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &events, wait);
    }
}

esp_err_t taiko_hit_led_start(void) {
    if (__atomic_load_n(
            &s_hit_led_task_handle, __ATOMIC_ACQUIRE) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreatePinnedToCore(
            hit_led_task, "hit_led", HIT_LED_TASK_STACK_SIZE, NULL,
            HIT_LED_TASK_PRIORITY, &s_hit_led_task_handle,
            HIT_LED_TASK_CORE) != pdPASS) {
        s_hit_led_task_handle = NULL;
        ESP_LOGE(TAG, "indicator task could not be created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void taiko_hit_led_notify(int player, taiko_zone_t zone) {
    if (player < 0 || player >= 2 || zone < TAIKO_ZONE_LEFT_DON ||
        zone > TAIKO_ZONE_RIGHT_KA) {
        return;
    }
    const uint32_t led =
        (uint32_t)player * TAIKO_CHANNELS_PER_PLAYER +
        kLedOffsetByZone[(size_t)zone];
    const uint32_t event = 1U << led;

    const TaskHandle_t task = __atomic_load_n(
        &s_hit_led_task_handle, __ATOMIC_ACQUIRE);
    if (task != NULL) {
        (void)xTaskNotify(task, event, eSetBits);
    }
}
