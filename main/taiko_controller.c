#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "class/hid/hid_device.h"
#include "driver/gpio.h"

#define DEBUG_PIN_1 GPIO_NUM_1  // HIGH if ADC read timeout
#define DEBUG_PIN_2 GPIO_NUM_2  // HIGH if host not ready (tud_hid_ready false)
#include "esp_adc/adc_continuous.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"  // for esp_rom_delay_us
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

// static const char *TAG = "taiko_gamepad"; // For debugging only

/************* TinyUSB descriptors ****************/

enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define EPNUM_HID 0x81
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

#define USB_VID 0x4869
#define USB_PID 0x4869
#define USB_REPORT_INTERVAL_US 2000  // Must be multiple of 100 for integer samples
#define AXIS_MAX 127
#define AXIS_MIN (-127)

// ADC/DMA sampling + lightweight amplitude extraction
#define PLAYERS 2
#define CHANNELS_PER_PLAYER 4
#define TOTAL_CHANNELS (PLAYERS * CHANNELS_PER_PLAYER)
#define PER_CHANNEL_SAMPLE_RATE_HZ 10000UL
// Derived: samples per CPU cycle = USB_REPORT_INTERVAL_US / 100
#define SAMPLES_PER_CPU_CYCLE (USB_REPORT_INTERVAL_US / 100)
#define DMA_CONVERSIONS_PER_FRAME (TOTAL_CHANNELS * SAMPLES_PER_CPU_CYCLE)
#define DMA_FRAME_BYTES (DMA_CONVERSIONS_PER_FRAME * SOC_ADC_DIGI_RESULT_BYTES)
#define DMA_STORE_BUFF_BYTES (DMA_FRAME_BYTES * 2)
#define ADC_READ_TIMEOUT_MS 1
#define ADC_ATTEN_DB ADC_ATTEN_DB_12
#define ADC_BIT_WIDTH ADC_BITWIDTH_12
#define POWER_RING_SIZE 3

#define BUTTON_Y_MASK (1u << 3)

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

// ADC pin map (ADC1 GPIOs on ESP32-S3; avoids USB DP/DM pins 20/19)
static const int kChannelGpios[TOTAL_CHANNELS] = {
    3, 4, 5, 6,  // P1 L-Don, L-Kat, R-Don, R-Kat
    7, 8, 9, 10  // P2 L-Don, L-Kat, R-Don, R-Kat
};

static adc_continuous_handle_t s_adc_handle;
static TaskHandle_t s_adc_task_handle;
static int8_t s_channel_index_by_adc[SOC_ADC_CHANNEL_NUM(0) + 1];

// Double buffer for ADC accumulation (swap each CPU cycle for stable 10 samples)
// Buffer 0 and 1 alternate: ADC writes to one while CPU reads the other
static uint32_t s_adc_sums[2][TOTAL_CHANNELS];
static volatile int s_adc_write_buf = 0;
static portMUX_TYPE s_adc_mux = portMUX_INITIALIZER_UNLOCKED;

// Ring buffer for power history (square of sum * sensitivity)
static uint64_t s_power_ring[TOTAL_CHANNELS][POWER_RING_SIZE];
static size_t s_power_ring_idx = 0;

// Per-channel sensitivity scaler (tunable)
static float s_channel_sensitivity[TOTAL_CHANNELS] = {
    1.0f, 15.0f, 1.0f, 15.0f,
    1.0f, 15.0f, 1.0f, 15.0f,
};
// Power clamp: values >= this map to max axis value
static const uint64_t POWER_CLAMP = 10000000ULL;  // 1e7

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
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, false, sizeof(hid_report_descriptor), EPNUM_HID, sizeof(taiko_hid_report_t), 1),
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

/********* ADC continuous sampling ***************/

static inline uint32_t frame_sample_freq_hz(void) {
    return PER_CHANNEL_SAMPLE_RATE_HZ * TOTAL_CHANNELS;
}

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
        pattern[i].atten = ADC_ATTEN_DB;
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
        // ESP_LOGE(TAG, "no memory for ADC buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Drain all available frames (non-blocking after first)
        bool first_read = true;
        while (true) {
            uint32_t out = 0;
            // First read can wait, subsequent reads are non-blocking
            esp_err_t ret = adc_continuous_read(s_adc_handle, raw, DMA_FRAME_BYTES, &out,
                                                 first_read ? ADC_READ_TIMEOUT_MS : 0);
            first_read = false;
            if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_INVALID_STATE) {
                break;  // No more data available
            }
            gpio_set_level(DEBUG_PIN_1, ret == ESP_OK ? 0 : 1);  // LOW=ok, HIGH=error
            if (ret != ESP_OK || out < SOC_ADC_DIGI_RESULT_BYTES) {
                break;
            }

            // Accumulate samples directly into sums
            const adc_digi_output_data_t *items = (const adc_digi_output_data_t *)raw;
            size_t convs = out / SOC_ADC_DIGI_RESULT_BYTES;

            portENTER_CRITICAL(&s_adc_mux);
            int buf = s_adc_write_buf;
            for (size_t i = 0; i < convs; ++i) {
                uint8_t adc_ch = items[i].type2.channel;
                if (adc_ch >= (SOC_ADC_CHANNEL_NUM(0) + 1)) {
                    continue;
                }
                int idx = s_channel_index_by_adc[adc_ch];
                if (idx < 0 || idx >= TOTAL_CHANNELS) {
                    continue;
                }
                s_adc_sums[buf][idx] += items[i].type2.data;
            }
            portEXIT_CRITICAL(&s_adc_mux);
        }
    }
}

/********* Application ***************/

static int8_t clamp_axis(int32_t v) {
    if (v > AXIS_MAX) {
        return AXIS_MAX;
    }
    if (v < AXIS_MIN) {
        return AXIS_MIN;
    }
    return (int8_t)v;
}

static taiko_hid_report_t consume_frames_and_build_report(void) {
    // Swap buffers: ADC will write to the other buffer, we read from current
    // This introduces 1ms latency but guarantees stable 10 samples per channel
    portENTER_CRITICAL(&s_adc_mux);
    int read_buf = s_adc_write_buf;
    s_adc_write_buf = 1 - read_buf;
    portEXIT_CRITICAL(&s_adc_mux);

    // Calculate square of sum for each channel and store in ring buffer
    for (int ch = 0; ch < TOTAL_CHANNELS; ++ch) {
        uint64_t sum = s_adc_sums[read_buf][ch] * s_channel_sensitivity[ch];
        s_power_ring[ch][s_power_ring_idx] = sum * sum;
        s_adc_sums[read_buf][ch] = 0;
    }
    s_power_ring_idx = (s_power_ring_idx + 1) % POWER_RING_SIZE;

    // Find max power from ring buffer for each channel
    uint64_t max_power[TOTAL_CHANNELS];
    for (int ch = 0; ch < TOTAL_CHANNELS; ++ch) {
        max_power[ch] = 0;
        for (int i = 0; i < POWER_RING_SIZE; ++i) {
            if (s_power_ring[ch][i] > max_power[ch]) {
                max_power[ch] = s_power_ring[ch][i];
            }
        }
    }

    // Map to axis values: clamp at POWER_CLAMP (1e7)
    int32_t scaled_vals[TOTAL_CHANNELS];
    for (int ch = 0; ch < TOTAL_CHANNELS; ++ch) {
        if (max_power[ch] >= POWER_CLAMP) {
            scaled_vals[ch] = AXIS_MAX;
        } else {
            scaled_vals[ch] = (int32_t)((max_power[ch] * AXIS_MAX) / POWER_CLAMP);
        }
    }

    taiko_hid_report_t rpt = {0};
    // For each drum, only the single highest channel fires; don = positive, kat = negative
    // P1: find winner among L-Don(0), L-Kat(1), R-Don(2), R-Kat(3)
    int p1_winner = 0;
    for (int i = 1; i < 4; i++) {
        if (scaled_vals[i] > scaled_vals[p1_winner]) p1_winner = i;
    }
    switch (p1_winner) {
        case 0: rpt.x =  clamp_axis( scaled_vals[0]); break;  // L-Don → +x
        case 1: rpt.x =  clamp_axis(-scaled_vals[1]); break;  // L-Kat → -x
        case 2: rpt.y =  clamp_axis( scaled_vals[2]); break;  // R-Don → +y
        case 3: rpt.y =  clamp_axis(-scaled_vals[3]); break;  // R-Kat → -y
    }
    // P2: find winner among L-Don(4), L-Kat(5), R-Don(6), R-Kat(7)
    int p2_winner = 4;
    for (int i = 5; i < 8; i++) {
        if (scaled_vals[i] > scaled_vals[p2_winner]) p2_winner = i;
    }
    switch (p2_winner) {
        case 4: rpt.rx = clamp_axis( scaled_vals[4]); break;  // L-Don → +rx
        case 5: rpt.rx = clamp_axis(-scaled_vals[5]); break;  // L-Kat → -rx
        case 6: rpt.ry = clamp_axis( scaled_vals[6]); break;  // R-Don → +ry
        case 7: rpt.ry = clamp_axis(-scaled_vals[7]); break;  // R-Kat → -ry
    }

    // Pulse Y button at 1 Hz (0.5 s on / 0.5 s off) to keep some activity visible
    uint32_t phase = esp_timer_get_time() % 1000000;
    if (phase < 500000) {
        rpt.buttons |= BUTTON_Y_MASK;
    }

    return rpt;
}

// Timer callback for HID reports (runs in esp_timer task context)
static void hid_report_timer_cb(void *arg) {
    (void)arg;
    if (tud_mounted()) {
        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        bool host_ready = tud_hid_ready();
        gpio_set_level(DEBUG_PIN_2, host_ready ? 0 : 1);  // HIGH if not ready

        if (host_ready) {
            taiko_hid_report_t rpt = consume_frames_and_build_report();
            tud_hid_report(0x01, &rpt, sizeof(rpt));
        }
    }
}

void app_main(void) {
    // Debug GPIOs for oscilloscope
    // PIN_1: HIGH if ADC read timeout
    // PIN_2: HIGH if host not ready (tud_hid_ready false)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DEBUG_PIN_1) | (1ULL << DEBUG_PIN_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // ESP_LOGI(TAG, "USB initialization (gamepad, VID 0x%04X PID 0x%04X)", USB_VID, USB_PID);
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
// #if TUD_OPT_HIGH_SPEED
//     tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
// #endif

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // ESP_LOGI(TAG, "Setting up ADC DMA for %d channels @ %lu Hz per channel", TOTAL_CHANNELS, (unsigned long)PER_CHANNEL_SAMPLE_RATE_HZ);
    ESP_ERROR_CHECK(init_adc());

    const UBaseType_t prio_adc = 3;
    if (xTaskCreatePinnedToCore(adc_reader_task, "adc_reader", 4096, NULL, prio_adc, &s_adc_task_handle, 0) != pdPASS) {
        // ESP_LOGE(TAG, "failed to start ADC reader task");
        return;
    }

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = on_conv_done_cb,
        .on_pool_ovf = NULL,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(s_adc_handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));
    // ESP_LOGI(TAG, "ADC DMA started");

    // Wait for ADC to fill first buffer before starting periodic reports
    vTaskDelay(pdMS_TO_TICKS(2));

    // Create precise 1ms periodic timer using hardware timer
    const esp_timer_create_args_t timer_args = {
        .callback = hid_report_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,  // Run in esp_timer task (not ISR)
        .name = "hid_report",
    };
    esp_timer_handle_t hid_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &hid_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(hid_timer, USB_REPORT_INTERVAL_US));

    // Main task can now sleep forever - timer handles HID reports
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
