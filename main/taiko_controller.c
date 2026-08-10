#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"
#include "taiko_hit_led.h"
#include "taiko_hit_processor.h"
#include "taiko_reports.h"
#include "taiko_usb.h"

#define USB_REPORT_INTERVAL_US 1000

/************* V2 digital controls ****************/

#define BUTTON_DEBOUNCE_REPORTS 5U
#define DIP_SETTLE_MS 5U

typedef enum {
    INPUT_DPAD_UP = 0,
    INPUT_DPAD_RIGHT,
    INPUT_DPAD_DOWN,
    INPUT_DPAD_LEFT,
    INPUT_FACE_UP,
    INPUT_FACE_RIGHT,
    INPUT_FACE_DOWN,
    INPUT_FACE_LEFT,
    INPUT_L1,
    INPUT_R1,
    INPUT_L2,
    INPUT_R2,
    INPUT_SELECT,
    INPUT_START,
    INPUT_HOME,
    CONTROLLER_INPUT_COUNT,
} controller_input_t;

typedef struct {
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_reports;
} button_filter_t;

static const gpio_num_t kControllerInputGpios[CONTROLLER_INPUT_COUNT] = {
    GPIO_NUM_11,  // D-pad up
    GPIO_NUM_12,  // D-pad right
    GPIO_NUM_13,  // D-pad down
    GPIO_NUM_14,  // D-pad left
    GPIO_NUM_15,  // Face up / north
    GPIO_NUM_16,  // Face right / east
    GPIO_NUM_17,  // Face down / south
    GPIO_NUM_18,  // Face left / west
    GPIO_NUM_47,  // L1
    GPIO_NUM_48,  // R1
    GPIO_NUM_1,   // L2
    GPIO_NUM_2,   // R2
    GPIO_NUM_43,  // Select; UART0 console is disabled in sdkconfig.defaults
    GPIO_NUM_44,  // Start
    GPIO_NUM_0,   // Home at runtime; BOOT when held during power-on
};

static const gpio_num_t kDipGpios[4] = {
    GPIO_NUM_39,
    GPIO_NUM_40,
    GPIO_NUM_41,
    GPIO_NUM_42,
};

#define CONTROLLER_INPUT_MASK                                                \
    ((1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_1) |                           \
     (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_11) |                          \
     (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_13) |                         \
     (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_15) |                         \
     (1ULL << GPIO_NUM_16) | (1ULL << GPIO_NUM_17) |                         \
     (1ULL << GPIO_NUM_18) | (1ULL << GPIO_NUM_43) |                         \
     (1ULL << GPIO_NUM_44) | (1ULL << GPIO_NUM_47) |                         \
     (1ULL << GPIO_NUM_48))

#define DIP_INPUT_MASK                                                       \
    ((1ULL << GPIO_NUM_39) | (1ULL << GPIO_NUM_40) |                         \
     (1ULL << GPIO_NUM_41) | (1ULL << GPIO_NUM_42))

#define DIP_P1_LONG_TAIL (1U << 0)
#define DIP_P2_LONG_TAIL (1U << 1)
#define DIP_MODE_SHIFT 2U
#define DIP_MODE_MASK 0x03U

/************* ADC and detector configuration ****************/

#define PLAYERS 2
#define CHANNELS_PER_PLAYER TAIKO_CHANNELS_PER_PLAYER
#define TOTAL_CHANNELS (PLAYERS * CHANNELS_PER_PLAYER)

// ESP32-S3's ADC digital controller uses a 2.5 MHz trigger timer. Arcade mode
// samples eight channels at 83,333.333 conversions/s. Single-player modes use
// only P1's four channels at 41,666.667 conversions/s. Both yield the detector's
// calibrated 10,416.667 samples/s/channel.
#define ADC_ARCADE_REQUESTED_CONVERSION_RATE_HZ 83333UL
#define ADC_SINGLE_PLAYER_REQUESTED_CONVERSION_RATE_HZ 41666UL
#define ADC_SCANS_PER_DMA_FRAME 8
#define MAX_DMA_CONVERSIONS_PER_FRAME                                      \
    (TOTAL_CHANNELS * ADC_SCANS_PER_DMA_FRAME)
#define MAX_DMA_FRAME_BYTES                                                \
    (MAX_DMA_CONVERSIONS_PER_FRAME * SOC_ADC_DIGI_RESULT_BYTES)
#define ADC_READ_TIMEOUT_MS 2
#define ADC_ATTEN_DB ADC_ATTEN_DB_12
#define ADC_BIT_WIDTH ADC_BITWIDTH_12
#define HIT_SENSITIVITY TAIKO_SENSITIVITY_BALANCED
#define ADC_RAW_LEVELS (1U << 12)
#define NOMINAL_ADC_FULL_SCALE_MV 3100U

// Each player's four channels are contiguous. Within a player, mechanically
// coupled same-side sensors are adjacent in the ADC pattern.
static const int kChannelGpios[TOTAL_CHANNELS] = {
    3, 4, 5, 6,  // P1 L-Don, L-Ka, R-Don, R-Ka
    7, 8, 9, 10  // P2 L-Don, L-Ka, R-Don, R-Ka
};

static const char *TAG = "taiko_controller";
static adc_continuous_handle_t s_adc_handle;
static TaskHandle_t s_adc_task_handle;
static int8_t s_pattern_slot_by_adc_channel[SOC_ADC_CHANNEL_NUM(0)];
static volatile bool s_adc_pool_overflow;
static taiko_hit_processor_t s_hit_processors[PLAYERS];
static uint32_t s_published_outputs[PLAYERS];
static uint16_t s_adc_millivolts[ADC_RAW_LEVELS];
static button_filter_t s_button_filters[CONTROLLER_INPUT_COUNT];
static uint8_t s_dip_switches;
static taiko_controller_mode_t s_controller_mode;
static int s_active_players = PLAYERS;
static int s_active_channels = TOTAL_CHANNELS;
static uint32_t s_adc_conversion_rate_hz =
    ADC_ARCADE_REQUESTED_CONVERSION_RATE_HZ;
static uint32_t s_dma_frame_bytes = MAX_DMA_FRAME_BYTES;

/********* V2 digital controls ***************/

static bool read_active_low(gpio_num_t gpio) {
    return gpio_get_level(gpio) == 0;
}

static uint8_t read_dip_switches(void) {
    uint8_t switches = 0;
    for (size_t index = 0; index < sizeof(kDipGpios) / sizeof(kDipGpios[0]);
         ++index) {
        if (read_active_low(kDipGpios[index])) {
            switches |= (uint8_t)(1U << index);
        }
    }
    return switches;
}

static void initialize_button_filters(void) {
    for (size_t index = 0; index < CONTROLLER_INPUT_COUNT; ++index) {
        const bool pressed = read_active_low(kControllerInputGpios[index]);
        s_button_filters[index] = (button_filter_t){
            .stable_pressed = pressed,
            .candidate_pressed = pressed,
            .candidate_reports = BUTTON_DEBOUNCE_REPORTS,
        };
    }
}

static void sample_button_filters(void) {
    for (size_t index = 0; index < CONTROLLER_INPUT_COUNT; ++index) {
        button_filter_t *filter = &s_button_filters[index];
        const bool pressed = read_active_low(kControllerInputGpios[index]);
        if (pressed != filter->candidate_pressed) {
            filter->candidate_pressed = pressed;
            filter->candidate_reports = 1;
            continue;
        }
        if (filter->candidate_reports < BUTTON_DEBOUNCE_REPORTS) {
            filter->candidate_reports++;
        }
        if (filter->candidate_reports >= BUTTON_DEBOUNCE_REPORTS) {
            filter->stable_pressed = filter->candidate_pressed;
        }
    }
}

static bool button_pressed(controller_input_t input) {
    return s_button_filters[input].stable_pressed;
}

static uint8_t build_dpad_bitmap(void) {
    uint8_t dpad = 0;
    if (button_pressed(INPUT_DPAD_UP)) {
        dpad |= TAIKO_DPAD_UP;
    }
    if (button_pressed(INPUT_DPAD_RIGHT)) {
        dpad |= TAIKO_DPAD_RIGHT;
    }
    if (button_pressed(INPUT_DPAD_DOWN)) {
        dpad |= TAIKO_DPAD_DOWN;
    }
    if (button_pressed(INPUT_DPAD_LEFT)) {
        dpad |= TAIKO_DPAD_LEFT;
    }
    return dpad;
}

static uint16_t build_button_bitmap(void) {
    uint16_t buttons = 0;
    if (button_pressed(INPUT_FACE_UP)) {
        buttons |= TAIKO_BUTTON_FACE_UP;
    }
    if (button_pressed(INPUT_FACE_RIGHT)) {
        buttons |= TAIKO_BUTTON_FACE_RIGHT;
    }
    if (button_pressed(INPUT_FACE_DOWN)) {
        buttons |= TAIKO_BUTTON_FACE_DOWN;
    }
    if (button_pressed(INPUT_FACE_LEFT)) {
        buttons |= TAIKO_BUTTON_FACE_LEFT;
    }
    if (button_pressed(INPUT_L1)) {
        buttons |= TAIKO_BUTTON_L1;
    }
    if (button_pressed(INPUT_R1)) {
        buttons |= TAIKO_BUTTON_R1;
    }
    if (button_pressed(INPUT_L2)) {
        buttons |= TAIKO_BUTTON_L2;
    }
    if (button_pressed(INPUT_R2)) {
        buttons |= TAIKO_BUTTON_R2;
    }
    if (button_pressed(INPUT_SELECT)) {
        buttons |= TAIKO_BUTTON_SELECT;
    }
    if (button_pressed(INPUT_START)) {
        buttons |= TAIKO_BUTTON_START;
    }
    if (button_pressed(INPUT_HOME)) {
        buttons |= TAIKO_BUTTON_HOME;
    }
    return buttons;
}

static void initialize_board_inputs(void) {
    const gpio_config_t input_config = {
        .pin_bit_mask = CONTROLLER_INPUT_MASK | DIP_INPUT_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input_config));

    // All controls have external 10 kOhm pull-ups. This short delay lets the
    // four static DIP levels settle before they select detector profiles.
    vTaskDelay(pdMS_TO_TICKS(DIP_SETTLE_MS));
    s_dip_switches = read_dip_switches();
    initialize_button_filters();

    static const char *const mode_names[] = {
        "Arcade",
        "PC",
        "Nintendo Switch",
        "PS4",
    };
    s_controller_mode = (taiko_controller_mode_t)(
        (s_dip_switches >> DIP_MODE_SHIFT) & DIP_MODE_MASK);
    s_active_players =
        s_controller_mode == TAIKO_CONTROLLER_MODE_ARCADE ? PLAYERS : 1;
    s_active_channels = s_active_players * CHANNELS_PER_PLAYER;
    s_adc_conversion_rate_hz =
        s_active_players == PLAYERS
            ? ADC_ARCADE_REQUESTED_CONVERSION_RATE_HZ
            : ADC_SINGLE_PLAYER_REQUESTED_CONVERSION_RATE_HZ;
    s_dma_frame_bytes = (uint32_t)s_active_channels * ADC_SCANS_PER_DMA_FRAME *
                        SOC_ADC_DIGI_RESULT_BYTES;
    ESP_LOGI(TAG,
             "DIP: P1 long-tail=%s, P2 long-tail=%s, mode=%s, players=%d",
             (s_dip_switches & DIP_P1_LONG_TAIL) != 0 ? "on" : "off",
             (s_dip_switches & DIP_P2_LONG_TAIL) != 0 ? "on" : "off",
             mode_names[s_controller_mode], s_active_players);
}

/********* ADC continuous sampling ***************/

static esp_err_t init_adc(void) {
    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = s_dma_frame_bytes * 8U,
        .conv_frame_size = s_dma_frame_bytes,
        .flags = {.flush_pool = 1},
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_config, &s_adc_handle));

    adc_digi_pattern_config_t pattern[TOTAL_CHANNELS] = {0};
    memset(s_pattern_slot_by_adc_channel, -1,
           sizeof(s_pattern_slot_by_adc_channel));

    for (int slot = 0; slot < s_active_channels; ++slot) {
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
        .pattern_num = (uint32_t)s_active_channels,
        .adc_pattern = pattern,
        .sample_freq_hz = s_adc_conversion_rate_hz,
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
    for (int player = 0; player < s_active_players; ++player) {
        const uint16_t *player_samples =
            &scan[player * CHANNELS_PER_PLAYER];
        taiko_hit_event_t hit_event;
        if (taiko_hit_processor_push(
                &s_hit_processors[player], player_samples, &hit_event)) {
            taiko_hit_led_notify(player, hit_event.zone);
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
        s_dma_frame_bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (raw == NULL) {
        ESP_LOGE(TAG, "ADC DMA read buffer allocation failed");
        vTaskDelete(NULL);
        return;
    }

    uint16_t scan[TOTAL_CHANNELS] = {0};
    size_t expected_slot = 0;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (__atomic_exchange_n(
                &s_adc_pool_overflow, false, __ATOMIC_ACQ_REL)) {
            ESP_LOGW(TAG, "ADC DMA pool overflow");
        }

        bool first_read = true;
        while (true) {
            uint32_t bytes_read = 0;
            const esp_err_t result = adc_continuous_read(
                s_adc_handle, raw, s_dma_frame_bytes, &bytes_read,
                first_read ? ADC_READ_TIMEOUT_MS : 0);
            first_read = false;

            if (result == ESP_ERR_TIMEOUT) {
                break;
            }
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(result));
                expected_slot = 0;
                break;
            }

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
                if (slot < 0 || slot >= s_active_channels) {
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
                if (expected_slot == (size_t)s_active_channels) {
                    process_complete_adc_scan(scan);
                    expected_slot = 0;
                }
            }
        }
    }
}

/********* USB report publication ***************/

static void apply_arcade_output(taiko_input_snapshot_t *input, int player,
                                taiko_hit_output_t output) {
    if (!output.active || output.axis_value == 0) {
        return;
    }

    int8_t *horizontal =
        player == 0 ? &input->arcade_p1_x : &input->arcade_p2_x;
    int8_t *vertical =
        player == 0 ? &input->arcade_p1_y : &input->arcade_p2_y;
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

static uint8_t drum_button_for_zone(taiko_zone_t zone) {
    switch (zone) {
        case TAIKO_ZONE_LEFT_DON:
            return TAIKO_DRUM_LEFT_DON;
        case TAIKO_ZONE_RIGHT_DON:
            return TAIKO_DRUM_RIGHT_DON;
        case TAIKO_ZONE_LEFT_KA:
            return TAIKO_DRUM_LEFT_KA;
        case TAIKO_ZONE_RIGHT_KA:
            return TAIKO_DRUM_RIGHT_KA;
    }
    return 0;
}

static taiko_input_snapshot_t build_input_snapshot(void) {
    taiko_input_snapshot_t input = {
        .dpad = build_dpad_bitmap(),
        .buttons = build_button_bitmap(),
    };
    for (int player = 0; player < s_active_players; ++player) {
        const uint32_t encoded = __atomic_load_n(
            &s_published_outputs[player], __ATOMIC_ACQUIRE);
        const taiko_hit_output_t output = decode_output(encoded);
        if (s_controller_mode == TAIKO_CONTROLLER_MODE_ARCADE) {
            apply_arcade_output(&input, player, output);
        } else if (player == 0 && output.active && output.axis_value != 0) {
            input.drum_buttons |= drum_button_for_zone(output.zone);
        }
    }
    return input;
}

static void usb_report_timer_cb(void *argument) {
    (void)argument;
    sample_button_filters();
    const taiko_input_snapshot_t input = build_input_snapshot();
    (void)taiko_usb_send(&input);
}

void app_main(void) {
    initialize_board_inputs();
    ESP_ERROR_CHECK(taiko_usb_install(s_controller_mode));
    if (s_controller_mode == TAIKO_CONTROLLER_MODE_PS4 &&
        !taiko_usb_ps4_authentication_available()) {
        ESP_LOGW(TAG,
                 "PS4 USB reports enabled without licensed authentication; "
                 "native console sessions cannot remain authenticated");
    }

    for (int player = 0; player < s_active_players; ++player) {
        const uint8_t profile_switch =
            player == 0 ? DIP_P1_LONG_TAIL : DIP_P2_LONG_TAIL;
        const taiko_hit_config_t hit_config =
            (s_dip_switches & profile_switch) != 0
                ? taiko_hit_long_tail_config()
                : taiko_hit_config_for_sensitivity(HIT_SENSITIVITY);
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
        .callback = usb_report_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "usb_report",
    };
    esp_timer_handle_t hid_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &hid_timer));
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(hid_timer, USB_REPORT_INTERVAL_US));

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
