#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_filter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "hal/adc_types.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "soc/soc_caps.h"

// ----------------------------- Configuration ------------------------------

#define PLAYERS 2
#define CHANNELS_PER_PLAYER 4
#define TOTAL_CHANNELS (PLAYERS * CHANNELS_PER_PLAYER)

// Sampling config
#define PER_CHANNEL_SAMPLE_RATE_HZ 10000UL
#define FRAME_SAMPLES_PER_CHANNEL 16UL   // 1.6 ms frame at 10 kHz
#define DMA_CONVERSIONS_PER_FRAME (TOTAL_CHANNELS * FRAME_SAMPLES_PER_CHANNEL)
#define DMA_FRAME_BYTES (DMA_CONVERSIONS_PER_FRAME * SOC_ADC_DIGI_RESULT_BYTES)
#define DMA_STORE_BUFF_BYTES (DMA_FRAME_BYTES * 4)
#define ADC_READ_TIMEOUT_MS 10

// HID output cadence
#define USB_REPORT_INTERVAL_MS 4   // 1 ms = 1000 Hz, 4 ms = 250 Hz

// Axis scaling
#define AXIS_RANGE 32767

// Analog front-end parameters
#define ADC_ATTEN_DB ADC_ATTEN_DB_11
#define ADC_BIT_WIDTH ADC_BITWIDTH_12

// DSP parameters
#define HPF_CUTOFF_HZ 20.0f
#define ENVELOPE_ATTACK_MS 0.8f
#define ENVELOPE_RELEASE_MS 6.0f
#define NOISE_TC_MS 120.0f
#define GATE_BASELINE 12.0f
#define GATE_MULTIPLIER 3.0f
#define GATE_HYSTERESIS 8.0f
#define ENVELOPE_FULL_SCALE 3500.0f

// Pin map (ADC1 GPIOs on ESP32-S3)
static const int kChannelGpios[TOTAL_CHANNELS] = {
    4, 5, 6, 7,  // P1 L-Don, L-Kat, R-Don, R-Kat
    8, 1, 9, 10  // P2 L-Don, L-Kat, R-Don, R-Kat
};

static const float kChannelGain[TOTAL_CHANNELS] = {
    1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
};

// ----------------------------- Types & globals -----------------------------

typedef struct {
    uint16_t samples[TOTAL_CHANNELS][FRAME_SAMPLES_PER_CHANNEL];
    size_t count;
    uint64_t timestamp_us;
} sample_frame_t;

typedef struct {
    float prev_raw;
    float hp;
    float env;
    float noise;
    bool active;
} channel_state_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t rx;
    int16_t ry;
    uint64_t timestamp_us;
} axis_snapshot_t;

typedef struct __attribute__((packed)) {
    int16_t x;
    int16_t y;
    int16_t rx;
    int16_t ry;
} hid_report_t;

static const char *TAG = "taiko";

static adc_continuous_handle_t s_adc_handle;
static TaskHandle_t s_adc_task_handle;
static RingbufHandle_t s_frame_ring;
static int8_t s_channel_index_by_adc[SOC_ADC_CHANNEL_NUM(0) + 1];
static portMUX_TYPE s_axis_spinlock = portMUX_INITIALIZER_UNLOCKED;
static axis_snapshot_t s_axis_snapshot = {0};

// TinyUSB descriptors
enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define EPNUM_HID 0x81

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define USB_VID 0x4869
#define USB_PID 0x4869

static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,       // USAGE (Gamepad)
    0xA1, 0x01,       // COLLECTION (Application)
    0x85, 0x01,       //   REPORT_ID (1)
    0x16, 0x01, 0x80, //   LOGICAL_MINIMUM (-32767)
    0x26, 0xFF, 0x7F, //   LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //   REPORT_SIZE (16)
    0x95, 0x04,       //   REPORT_COUNT (4 axes)
    0x09, 0x30,       //   USAGE (X)
    0x09, 0x31,       //   USAGE (Y)
    0x09, 0x33,       //   USAGE (Rx)
    0x09, 0x34,       //   USAGE (Ry)
    0x81, 0x02,       //   INPUT (Data,Var,Abs)
    0xC0              // END_COLLECTION
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
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, false, sizeof(hid_report_descriptor), EPNUM_HID, sizeof(hid_report_t), USB_REPORT_INTERVAL_MS),
};

static const char *hid_string_descriptor[] = {
    (const char[]){0x09, 0x04},  // 0: English (0x0409)
    "Taiko Community",           // 1: Manufacturer
    "Taiko Drum Controller",     // 2: Product
    "0001",                      // 3: Serial
    "Gamepad",                   // 4: HID Interface
};

// ----------------------------- Helpers -------------------------------------

static inline int16_t clamp_axis(int32_t v) {
    if (v > AXIS_RANGE) {
        return AXIS_RANGE;
    }
    if (v < -AXIS_RANGE) {
        return -AXIS_RANGE;
    }
    return (int16_t)v;
}

static inline uint32_t frame_sample_freq_hz(void) {
    return PER_CHANNEL_SAMPLE_RATE_HZ * TOTAL_CHANNELS;
}

// ----------------------------- USB callbacks -------------------------------

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

// ----------------------------- ADC + DSP -----------------------------------

static esp_err_t init_adc(void) {
    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = DMA_STORE_BUFF_BYTES,
        .conv_frame_size = DMA_FRAME_BYTES,
        .flags = {.flush_pool = 1},
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_config, &s_adc_handle));

    adc_digi_pattern_config_t pattern[TOTAL_CHANNELS] = {0};
    memset(s_channel_index_by_adc, -1, sizeof(s_channel_index_by_adc));

    for (int i = 0; i < TOTAL_CHANNELS; ++i) {
        adc_unit_t unit;
        adc_channel_t ch;
        ESP_ERROR_CHECK(adc_continuous_io_to_channel(kChannelGpios[i], &unit, &ch));
        s_channel_index_by_adc[ch] = i;
        pattern[i].atten = ADC_ATTEN_DB_12;
        pattern[i].channel = ch;
        pattern[i].unit = unit;
        pattern[i].bit_width = ADC_BIT_WIDTH;
    }

    adc_continuous_config_t dig_cfg = {
        .pattern_num = TOTAL_CHANNELS,
        .adc_pattern = pattern,
        .sample_freq_hz = frame_sample_freq_hz(),
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_ERROR_CHECK(adc_continuous_config(s_adc_handle, &dig_cfg));

    // Optional hardware IIR filter to shave off high-frequency noise
    bool filter_warned = false;
    for (int i = 0; i < TOTAL_CHANNELS; ++i) {
        adc_continuous_iir_filter_config_t filter_cfg = {
            .unit = ADC_UNIT_1,
            .channel = (adc_channel_t)pattern[i].channel,
            .coeff = ADC_DIGI_IIR_FILTER_COEFF_8,
        };
        adc_iir_filter_handle_t filter;
        esp_err_t ferr = adc_new_continuous_iir_filter(s_adc_handle, &filter_cfg, &filter);
        if (ferr == ESP_OK) {
            adc_continuous_iir_filter_enable(filter);
        } else if (!filter_warned) {
            ESP_LOGW(TAG, "IIR filter not enabled (err %s); continuing without HW filter", esp_err_to_name(ferr));
            filter_warned = true;
        }
    }

    return ESP_OK;
}

static bool IRAM_ATTR on_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    (void)handle;
    (void)edata;
    (void)user_data;
    BaseType_t awoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_adc_task_handle, &awoken);
    return awoken == pdTRUE;
}

static void adc_reader_task(void *arg) {
    uint8_t *raw = heap_caps_malloc(DMA_FRAME_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!raw) {
        ESP_LOGE(TAG, "no memory for ADC buffer");
        vTaskDelete(NULL);
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (true) {
            uint32_t out = 0;
            esp_err_t ret = adc_continuous_read(s_adc_handle, raw, DMA_FRAME_BYTES, &out, 0);
            if (ret == ESP_ERR_TIMEOUT) {
                break;
            }
            if (ret != ESP_OK || out < SOC_ADC_DIGI_RESULT_BYTES) {
                ESP_LOGW(TAG, "adc read err %d len %" PRIu32, ret, out);
                break;
            }

            sample_frame_t frame = {0};
            frame.count = FRAME_SAMPLES_PER_CHANNEL;
            frame.timestamp_us = esp_timer_get_time();
            size_t counts[TOTAL_CHANNELS] = {0};
            const adc_digi_output_data_t *items = (const adc_digi_output_data_t *)raw;
            size_t convs = out / SOC_ADC_DIGI_RESULT_BYTES;
            for (size_t i = 0; i < convs; ++i) {
                uint8_t adc_ch = items[i].type2.channel;
                if (adc_ch >= (SOC_ADC_CHANNEL_NUM(0) + 1)) {
                    continue;
                }
                int idx = s_channel_index_by_adc[adc_ch];
                if (idx < 0 || idx >= TOTAL_CHANNELS) {
                    continue;
                }
                size_t sample_idx = counts[idx];
                if (sample_idx < FRAME_SAMPLES_PER_CHANNEL) {
                    frame.samples[idx][sample_idx] = items[i].type2.data;
                    counts[idx]++;
                }
            }

            bool complete = true;
            for (int i = 0; i < TOTAL_CHANNELS; ++i) {
                if (counts[i] < FRAME_SAMPLES_PER_CHANNEL) {
                    complete = false;
                    break;
                }
            }
            if (complete) {
                if (xRingbufferSend(s_frame_ring, &frame, sizeof(frame), 0) != pdTRUE) {
                    ESP_LOGW(TAG, "frame ring full, dropping");
                }
            } else {
                ESP_LOGW(TAG, "incomplete frame, dropping");
            }
        }
    }
}

static void signal_process_task(void *arg) {
    channel_state_t state[TOTAL_CHANNELS] = {0};
    const float dt = 1.0f / (float)PER_CHANNEL_SAMPLE_RATE_HZ;
    const float pi = 3.14159265f;
    const float hp_coeff = 1.0f / (2.0f * pi * HPF_CUTOFF_HZ);
    const float hp_alpha = hp_coeff / (hp_coeff + dt);
    const float env_attack_alpha = dt / (ENVELOPE_ATTACK_MS / 1000.0f + dt);
    const float env_release_alpha = dt / (ENVELOPE_RELEASE_MS / 1000.0f + dt);
    const float noise_alpha = dt / (NOISE_TC_MS / 1000.0f + dt);

    while (true) {
        size_t item_size = 0;
        sample_frame_t *frame = (sample_frame_t *)xRingbufferReceive(s_frame_ring, &item_size, portMAX_DELAY);
        if (!frame || item_size != sizeof(sample_frame_t)) {
            continue;
        }

        axis_snapshot_t snapshot = {0};
        for (size_t sample_idx = 0; sample_idx < frame->count; ++sample_idx) {
            for (int ch = 0; ch < TOTAL_CHANNELS; ++ch) {
                float x = (float)frame->samples[ch][sample_idx];

                float hp = hp_alpha * (state[ch].hp + x - state[ch].prev_raw);
                state[ch].hp = hp;
                state[ch].prev_raw = x;

                float rect = fabsf(hp);
                float env_alpha = rect > state[ch].env ? env_attack_alpha : env_release_alpha;
                state[ch].env += env_alpha * (rect - state[ch].env);
                state[ch].noise += noise_alpha * (rect - state[ch].noise);

                float gate = GATE_BASELINE + state[ch].noise * GATE_MULTIPLIER;
                float gate_on = gate + GATE_HYSTERESIS;
                if (!state[ch].active && state[ch].env > gate_on) {
                    state[ch].active = true;
                } else if (state[ch].active && state[ch].env < gate) {
                    state[ch].active = false;
                }

                float magnitude = state[ch].active ? fmaxf(state[ch].env - gate, 0.0f) : 0.0f;
                float scaled = magnitude * kChannelGain[ch];
                int32_t axis_val = (int32_t)((scaled >= ENVELOPE_FULL_SCALE ? AXIS_RANGE : (scaled / ENVELOPE_FULL_SCALE) * AXIS_RANGE));

                switch (ch) {
                    case 0: // P1 L Don
                        snapshot.x = clamp_axis(axis_val);
                        break;
                    case 1: // P1 L Kat
                        snapshot.x = clamp_axis(-axis_val);
                        break;
                    case 2: // P1 R Don
                        snapshot.y = clamp_axis(axis_val);
                        break;
                    case 3: // P1 R Kat
                        snapshot.y = clamp_axis(-axis_val);
                        break;
                    case 4: // P2 L Don
                        snapshot.rx = clamp_axis(axis_val);
                        break;
                    case 5: // P2 L Kat
                        snapshot.rx = clamp_axis(-axis_val);
                        break;
                    case 6: // P2 R Don
                        snapshot.ry = clamp_axis(axis_val);
                        break;
                    case 7: // P2 R Kat
                        snapshot.ry = clamp_axis(-axis_val);
                        break;
                    default:
                        break;
                }
            }
        }
        snapshot.timestamp_us = frame->timestamp_us;

        portENTER_CRITICAL(&s_axis_spinlock);
        s_axis_snapshot = snapshot;
        portEXIT_CRITICAL(&s_axis_spinlock);

        vRingbufferReturnItem(s_frame_ring, frame);
    }
}

static void usb_hid_task(void *arg) {
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(USB_REPORT_INTERVAL_MS > 0 ? USB_REPORT_INTERVAL_MS : 1);

    while (true) {
        axis_snapshot_t snap;
        portENTER_CRITICAL(&s_axis_spinlock);
        snap = s_axis_snapshot;
        portEXIT_CRITICAL(&s_axis_spinlock);

        hid_report_t rpt = {
            .x = snap.x,
            .y = snap.y,
            .rx = snap.rx,
            .ry = snap.ry,
        };

        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        if (tud_mounted()) {
            tud_hid_report(0x01, &rpt, sizeof(rpt));
        }

        vTaskDelayUntil(&last, period);
    }
}

// ----------------------------- USB init ------------------------------------

static esp_err_t init_usb(void) {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
#if TUD_OPT_HIGH_SPEED
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    return ESP_OK;
}

// ----------------------------- Entry point ---------------------------------

void app_main(void) {
    ESP_LOGI(TAG, "boot: %d channels @ %" PRIu32 " Hz each, frame %u samples", TOTAL_CHANNELS, PER_CHANNEL_SAMPLE_RATE_HZ, FRAME_SAMPLES_PER_CHANNEL);

    ESP_ERROR_CHECK(init_usb());
    ESP_ERROR_CHECK(init_adc());

    s_frame_ring = xRingbufferCreate(sizeof(sample_frame_t) * 4, RINGBUF_TYPE_NOSPLIT);
    if (!s_frame_ring) {
        ESP_LOGE(TAG, "failed to create ringbuffer");
        return;
    }

    xTaskCreatePinnedToCore(adc_reader_task, "adc_reader", 4096, NULL, configMAX_PRIORITIES - 1, &s_adc_task_handle, 0);
    xTaskCreatePinnedToCore(signal_process_task, "signal_proc", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    xTaskCreatePinnedToCore(usb_hid_task, "usb_hid", 2048, NULL, configMAX_PRIORITIES - 3, NULL, 1);

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = on_conv_done_cb,
        .on_pool_ovf = NULL,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(s_adc_handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));
}
