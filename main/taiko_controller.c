#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"
#include "taiko_hit_led.h"
#include "taiko_hit_processor.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

#define DEBUG_PIN_1 GPIO_NUM_1  // HIGH on ADC DMA/read error
#define DEBUG_PIN_2 GPIO_NUM_2  // HIGH while the USB HID host is not ready

/************* TinyUSB descriptors ****************/

enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

#define EPNUM_HID 0x81
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

#define USB_VID 0x4869
#define USB_PID 0x4869
#define USB_REPORT_INTERVAL_US 1000

/************* ADC and detector configuration ****************/

#define PLAYERS 2
#define CHANNELS_PER_PLAYER TAIKO_CHANNELS_PER_PLAYER
#define TOTAL_CHANNELS (PLAYERS * CHANNELS_PER_PLAYER)

// ESP32-S3's ADC digital controller uses a 2.5 MHz trigger timer. ESP-IDF
// accepts 83,333 here, programs interval 30, and the resulting conversion
// rate is 83,333.333 Hz (10,416.667 complete samples/s for each of 8 inputs).
#define ADC_REQUESTED_CONVERSION_RATE_HZ 83333UL
#define ADC_SCANS_PER_DMA_FRAME 8
#define DMA_CONVERSIONS_PER_FRAME (TOTAL_CHANNELS * ADC_SCANS_PER_DMA_FRAME)
#define DMA_FRAME_BYTES (DMA_CONVERSIONS_PER_FRAME * SOC_ADC_DIGI_RESULT_BYTES)
#define DMA_STORE_BUFFER_BYTES (DMA_FRAME_BYTES * 8)
#define ADC_READ_TIMEOUT_MS 2
#define ADC_ATTEN_DB ADC_ATTEN_DB_12
#define ADC_BIT_WIDTH ADC_BITWIDTH_12
#define HIT_SENSITIVITY TAIKO_SENSITIVITY_BALANCED
#define ADC_RAW_LEVELS (1U << 12)
#define NOMINAL_ADC_FULL_SCALE_MV 3100U

typedef struct __attribute__((packed)) {
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t rz;
    int8_t rx;
    int8_t ry;
    uint8_t hat;
    uint32_t buttons;
} taiko_hid_report_t;

// Each player's four channels are contiguous. Within a player, mechanically
// coupled same-side sensors are adjacent in the ADC pattern.
static const int kChannelGpios[TOTAL_CHANNELS] = {
    3, 4, 5, 6,  // P1 L-Don, L-Ka, R-Don, R-Ka
    7, 8, 9, 10  // P2 L-Don, L-Ka, R-Don, R-Ka
};

static adc_continuous_handle_t s_adc_handle;
static TaskHandle_t s_adc_task_handle;
static int8_t s_pattern_slot_by_adc_channel[SOC_ADC_CHANNEL_NUM(0)];
static volatile bool s_adc_pool_overflow;
static taiko_hit_processor_t s_hit_processors[PLAYERS];
static uint32_t s_published_outputs[PLAYERS];
static uint16_t s_adc_millivolts[ADC_RAW_LEVELS];

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(0x01))
};

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, false, sizeof(hid_report_descriptor),
                       EPNUM_HID, sizeof(taiko_hid_report_t), 1),
};

static const char *hid_string_descriptor[] = {
    (const char[]){0x09, 0x04},  // 0: English (0x0409)
    "Taiko Community",           // 1: Manufacturer
    "Taiko Controller",          // 2: Product
    "0001",                      // 3: Serial
    "Gamepad",                   // 4: HID Interface
};

/********* TinyUSB HID callbacks ***************/

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/********* ADC continuous sampling ***************/

static esp_err_t init_adc(void) {
    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = DMA_STORE_BUFFER_BYTES,
        .conv_frame_size = DMA_FRAME_BYTES,
        .flags = {.flush_pool = 1},
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_config, &s_adc_handle));

    adc_digi_pattern_config_t pattern[TOTAL_CHANNELS] = {0};
    memset(s_pattern_slot_by_adc_channel, -1,
           sizeof(s_pattern_slot_by_adc_channel));

    for (int slot = 0; slot < TOTAL_CHANNELS; ++slot) {
        adc_unit_t unit;
        adc_channel_t channel;
        ESP_ERROR_CHECK(adc_continuous_io_to_channel(
            kChannelGpios[slot], &unit, &channel));
        if (unit != ADC_UNIT_1 || channel >= SOC_ADC_CHANNEL_NUM(0)) {
            return ESP_ERR_INVALID_ARG;
        }
        s_pattern_slot_by_adc_channel[channel] = slot;
        pattern[slot].atten = ADC_ATTEN_DB;
        pattern[slot].channel = channel;
        pattern[slot].unit = unit;
        pattern[slot].bit_width = ADC_BIT_WIDTH;
    }

    adc_continuous_config_t digital_config = {
        .pattern_num = TOTAL_CHANNELS,
        .adc_pattern = pattern,
        .sample_freq_hz = ADC_REQUESTED_CONVERSION_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_ERROR_CHECK(adc_continuous_config(s_adc_handle, &digital_config));
    return ESP_OK;
}

static void init_adc_calibration_lut(void) {
    adc_cali_handle_t calibration = NULL;
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB,
        .bitwidth = ADC_BIT_WIDTH,
    };
    const bool calibrated =
        adc_cali_create_scheme_curve_fitting(
            &calibration_config, &calibration) == ESP_OK;

    for (uint32_t raw = 0; raw < ADC_RAW_LEVELS; ++raw) {
        int millivolts =
            (int)((raw * NOMINAL_ADC_FULL_SCALE_MV +
                   (ADC_RAW_LEVELS - 1U) / 2U) /
                  (ADC_RAW_LEVELS - 1U));
        if (calibrated) {
            int calibrated_millivolts = 0;
            if (adc_cali_raw_to_voltage(
                    calibration, (int)raw, &calibrated_millivolts) == ESP_OK &&
                calibrated_millivolts >= 0) {
                millivolts = calibrated_millivolts;
            }
        }
        s_adc_millivolts[raw] = (uint16_t)millivolts;
    }
}

static bool IRAM_ATTR on_conversion_done(
    adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t *event_data,
    void *user_data) {
    (void)handle;
    (void)event_data;
    (void)user_data;
    BaseType_t awoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_adc_task_handle, &awoken);
    return awoken == pdTRUE;
}

static bool IRAM_ATTR on_pool_overflow(
    adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t *event_data,
    void *user_data) {
    (void)handle;
    (void)event_data;
    (void)user_data;
    s_adc_pool_overflow = true;
    BaseType_t awoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_adc_task_handle, &awoken);
    return awoken == pdTRUE;
}

static uint32_t encode_output(taiko_hit_output_t output) {
    if (!output.active) {
        return 0;
    }
    return (1U << 31) | ((uint32_t)output.zone << 8) | output.axis_value;
}

static taiko_hit_output_t decode_output(uint32_t encoded) {
    if ((encoded & (1U << 31)) == 0) {
        return (taiko_hit_output_t){0};
    }
    return (taiko_hit_output_t){
        .active = true,
        .zone = (taiko_zone_t)((encoded >> 8) & 0xffU),
        .axis_value = (uint8_t)(encoded & 0xffU),
    };
}

static void process_complete_adc_scan(const uint16_t scan[TOTAL_CHANNELS]) {
    for (int player = 0; player < PLAYERS; ++player) {
        const uint16_t *player_samples =
            &scan[player * CHANNELS_PER_PLAYER];
        taiko_hit_event_t hit_event;
        if (taiko_hit_processor_push(
                &s_hit_processors[player], player_samples, &hit_event)) {
            taiko_hit_led_notify(hit_event.zone);
        }
        const taiko_hit_output_t output =
            taiko_hit_processor_get_output(&s_hit_processors[player]);
        __atomic_store_n(
            &s_published_outputs[player], encode_output(output), __ATOMIC_RELEASE);
    }
}

static void adc_reader_task(void *argument) {
    (void)argument;
    uint8_t *raw = heap_caps_malloc(
        DMA_FRAME_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (raw == NULL) {
        gpio_set_level(DEBUG_PIN_1, 1);
        vTaskDelete(NULL);
        return;
    }

    uint16_t scan[TOTAL_CHANNELS] = {0};
    size_t expected_slot = 0;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (__atomic_exchange_n(
                &s_adc_pool_overflow, false, __ATOMIC_ACQ_REL)) {
            gpio_set_level(DEBUG_PIN_1, 1);
        }

        bool first_read = true;
        while (true) {
            uint32_t bytes_read = 0;
            const esp_err_t result = adc_continuous_read(
                s_adc_handle, raw, DMA_FRAME_BYTES, &bytes_read,
                first_read ? ADC_READ_TIMEOUT_MS : 0);
            first_read = false;

            if (result == ESP_ERR_TIMEOUT) {
                break;
            }
            if (result != ESP_OK) {
                gpio_set_level(DEBUG_PIN_1, 1);
                expected_slot = 0;
                break;
            }
            gpio_set_level(DEBUG_PIN_1, 0);

            const adc_digi_output_data_t *items =
                (const adc_digi_output_data_t *)raw;
            const size_t conversions =
                bytes_read / SOC_ADC_DIGI_RESULT_BYTES;
            for (size_t index = 0; index < conversions; ++index) {
                const uint8_t adc_channel = items[index].type2.channel;
                if (adc_channel >= SOC_ADC_CHANNEL_NUM(0)) {
                    expected_slot = 0;
                    continue;
                }
                const int slot =
                    s_pattern_slot_by_adc_channel[adc_channel];
                if (slot < 0 || slot >= TOTAL_CHANNELS) {
                    expected_slot = 0;
                    continue;
                }

                // DMA reads can begin at any pattern position. Discard a
                // partial scan and synchronize on slot zero.
                if ((size_t)slot != expected_slot) {
                    expected_slot = 0;
                    if (slot != 0) {
                        continue;
                    }
                }

                const uint16_t raw_sample = items[index].type2.data;
                scan[slot] =
                    s_adc_millivolts[raw_sample < ADC_RAW_LEVELS
                                         ? raw_sample
                                         : ADC_RAW_LEVELS - 1U];
                expected_slot++;
                if (expected_slot == TOTAL_CHANNELS) {
                    process_complete_adc_scan(scan);
                    expected_slot = 0;
                }
            }
        }
    }
}

/********* HID report publication ***************/

static void apply_player_output(
    taiko_hid_report_t *report, int player, taiko_hit_output_t output) {
    if (!output.active || output.axis_value == 0) {
        return;
    }

    int8_t *horizontal = player == 0 ? &report->x : &report->rx;
    int8_t *vertical = player == 0 ? &report->y : &report->ry;
    const int8_t value = (int8_t)output.axis_value;
    switch (output.zone) {
        case TAIKO_ZONE_LEFT_DON:
            *horizontal = value;
            break;
        case TAIKO_ZONE_LEFT_KA:
            *horizontal = -value;
            break;
        case TAIKO_ZONE_RIGHT_DON:
            *vertical = value;
            break;
        case TAIKO_ZONE_RIGHT_KA:
            *vertical = -value;
            break;
    }
}

static taiko_hid_report_t build_hid_report(void) {
    taiko_hid_report_t report = {0};
    for (int player = 0; player < PLAYERS; ++player) {
        const uint32_t encoded = __atomic_load_n(
            &s_published_outputs[player], __ATOMIC_ACQUIRE);
        apply_player_output(&report, player, decode_output(encoded));
    }
    return report;
}

static void hid_report_timer_cb(void *argument) {
    (void)argument;
    if (!tud_mounted()) {
        return;
    }
    if (tud_suspended()) {
        tud_remote_wakeup();
    }

    const bool host_ready = tud_hid_ready();
    gpio_set_level(DEBUG_PIN_2, host_ready ? 0 : 1);
    if (host_ready) {
        const taiko_hid_report_t report = build_hid_report();
        tud_hid_report(0x01, &report, sizeof(report));
    }
}

void app_main(void) {
    const gpio_config_t debug_gpio_config = {
        .pin_bit_mask =
            (1ULL << DEBUG_PIN_1) | (1ULL << DEBUG_PIN_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&debug_gpio_config));

    tinyusb_config_t tinyusb_config = TINYUSB_DEFAULT_CONFIG();
    tinyusb_config.descriptor.device = &device_descriptor;
    tinyusb_config.descriptor.full_speed_config =
        hid_configuration_descriptor;
    tinyusb_config.descriptor.string = hid_string_descriptor;
    tinyusb_config.descriptor.string_count =
        sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
    ESP_ERROR_CHECK(tinyusb_driver_install(&tinyusb_config));

    const taiko_hit_config_t hit_config =
        taiko_hit_config_for_sensitivity(HIT_SENSITIVITY);
    for (int player = 0; player < PLAYERS; ++player) {
        taiko_hit_processor_init(&s_hit_processors[player], &hit_config);
    }
    // The indicator is deliberately optional; ADC and HID operation continues
    // even if its low-priority worker cannot be created.
    (void)taiko_hit_led_start();

    ESP_ERROR_CHECK(init_adc());
    init_adc_calibration_lut();
    const UBaseType_t adc_task_priority = 4;
    if (xTaskCreatePinnedToCore(
            adc_reader_task, "adc_reader", 4096, NULL, adc_task_priority,
            &s_adc_task_handle, 0) != pdPASS) {
        return;
    }

    const adc_continuous_evt_cbs_t callbacks = {
        .on_conv_done = on_conversion_done,
        .on_pool_ovf = on_pool_overflow,
    };
    ESP_ERROR_CHECK(
        adc_continuous_register_event_callbacks(s_adc_handle, &callbacks, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));

    // Let the baseline estimator observe quiet samples before publishing.
    vTaskDelay(pdMS_TO_TICKS(2));

    const esp_timer_create_args_t timer_args = {
        .callback = hid_report_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "hid_report",
    };
    esp_timer_handle_t hid_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &hid_timer));
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(hid_timer, USB_REPORT_INTERVAL_US));

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
