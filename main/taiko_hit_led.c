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

enum {
    HIT_LED_EVENT_DON = 1U << 0,
    HIT_LED_EVENT_KA = 1U << 1,
};

static const char *TAG = "taiko_hit_led";
static TaskHandle_t s_hit_led_task_handle;

// The supplied XL-5050RGBC-WS2812B datasheet identifies bits by their high
// time. These values also keep the complete frame near its specified 800 kHz
// data rate.
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
    .duration0 = 500,  // 50 us low
    .level1 = 0,
    .duration1 = 500,  // 50 us low
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

    // The datasheet asks for at least 100 us low, even though its timing table
    // separately lists 80 us. Use the stricter value.
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

static esp_err_t write_led_color(rmt_channel_handle_t channel,
                                 rmt_encoder_handle_t encoder,
                                 uint32_t active_colors) {
    // The device consumes one pixel as G, R, B, most-significant bit first.
    const uint8_t grb[3] = {
        0,
        (active_colors & HIT_LED_EVENT_DON) != 0
            ? HIT_LED_CHANNEL_LEVEL
            : 0,
        (active_colors & HIT_LED_EVENT_KA) != 0
            ? HIT_LED_CHANNEL_LEVEL
            : 0,
    };
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

static TickType_t next_deadline_wait(TickType_t now,
                                     bool don_active,
                                     TickType_t don_until,
                                     bool ka_active,
                                     TickType_t ka_until) {
    TickType_t wait = portMAX_DELAY;
    if (don_active) {
        wait = don_until - now;
    }
    if (ka_active) {
        const TickType_t ka_wait = ka_until - now;
        if (wait == portMAX_DELAY || ka_wait < wait) {
            wait = ka_wait;
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

    bool don_active = false;
    bool ka_active = false;
    TickType_t don_until = 0;
    TickType_t ka_until = 0;
    uint32_t displayed_colors = UINT32_MAX;
    uint32_t events = 0;
    const TickType_t hold_ticks = pdMS_TO_TICKS(HIT_LED_HOLD_MS);

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if ((events & HIT_LED_EVENT_DON) != 0) {
            don_active = true;
            don_until = now + hold_ticks;
        }
        if ((events & HIT_LED_EVENT_KA) != 0) {
            ka_active = true;
            ka_until = now + hold_ticks;
        }

        if (don_active && deadline_reached(now, don_until)) {
            don_active = false;
        }
        if (ka_active && deadline_reached(now, ka_until)) {
            ka_active = false;
        }

        const uint32_t active_colors =
            (don_active ? HIT_LED_EVENT_DON : 0) |
            (ka_active ? HIT_LED_EVENT_KA : 0);
        if (active_colors != displayed_colors) {
            const esp_err_t write_result =
                write_led_color(channel, encoder, active_colors);
            if (write_result != ESP_OK) {
                ESP_LOGW(TAG, "color update failed: %s",
                         esp_err_to_name(write_result));
            }
            displayed_colors = active_colors;
        }

        const TickType_t wait = next_deadline_wait(
            now, don_active, don_until, ka_active, ka_until);
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

void taiko_hit_led_notify(taiko_zone_t zone) {
    uint32_t event = 0;
    switch (zone) {
        case TAIKO_ZONE_LEFT_DON:
        case TAIKO_ZONE_RIGHT_DON:
            event = HIT_LED_EVENT_DON;
            break;
        case TAIKO_ZONE_LEFT_KA:
        case TAIKO_ZONE_RIGHT_KA:
            event = HIT_LED_EVENT_KA;
            break;
    }

    const TaskHandle_t task = __atomic_load_n(
        &s_hit_led_task_handle, __ATOMIC_ACQUIRE);
    if (event != 0 && task != NULL) {
        (void)xTaskNotify(task, event, eSetBits);
    }
}
