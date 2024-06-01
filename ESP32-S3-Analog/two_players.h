#include "driver/adc.h"
// #include "driver/i2s.h"
#include "freertos/FreeRTOS.h"

#include "params.h"

// Sensitivity multipliers for each channel, 1.0 as the baseline.
#define P1_L_DON_SENS 10.0
#define P1_L_KAT_SENS 20.0
#define P1_R_DON_SENS 10.0
#define P1_R_KAT_SENS 20.0
#define P2_L_DON_SENS 1.0
#define P2_L_KAT_SENS 1.0
#define P2_R_DON_SENS 1.0
#define P2_R_KAT_SENS 1.0

#define PLAYERS 2
#define BUF_LENGH 1024
#define CONV_NUM BUF_LENGH * PLAYERS / CHANNELS

#define TIMES              256
#define GET_UNIT(x)        ((x>>3) & 0x1)

const byte inPins[PLAYERS][CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN, 
    P2_L_DON_IN, P2_L_KAT_IN, P2_R_DON_IN, P2_R_KAT_IN
};

const float sensitivities[PLAYERS][CHANNELS] = {
    P1_L_DON_SENS, P1_L_KAT_SENS, P1_R_DON_SENS, P1_R_KAT_SENS,
    P2_L_DON_SENS, P2_L_KAT_SENS, P2_R_DON_SENS, P2_R_KAT_SENS};

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[PLAYERS][CHANNELS];
unsigned long power[PLAYERS][CHANNELS];

uint axisValues[PLAYERS][CHANNELS] = {0, 0, 0, 0, 0, 0, 0, 0};

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, false, false, false,
                   false, false);

uint maxVal[PLAYERS] = {0, 0};
uint maxIndex[PLAYERS] = {-1, -1};

adc_channel_t adcChannles[PLAYERS][CHANNELS] = {
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
    ADC_CHANNEL_0,
    ADC_CHANNEL_8,
    ADC_CHANNEL_9
};

static void adc_init(adc_channel_t *channel, uint8_t channel_num)
{
    adc_digi_init_config_t adc_dma_config = {
        .max_store_buf_size = BUF_LENGH,
        .conv_num_each_intr = CONV_NUM,
        .adc1_chan_mask = BIT3 | BIT4 | BIT5 | BIT6 | BIT7 | BIT0 | BIT8 | BIT9,
        .adc2_chan_mask = 0
    };
    ESP_ERROR_CHECK(adc_digi_initialize(&adc_dma_config));

    adc_digi_configuration_t dig_cfg = {
        .conv_limit_en = 1,
        .conv_limit_num = 4,
        .sample_freq_hz = 2 * 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = channel_num;
    for (int i = 0; i < channel_num; i++) {
        uint8_t unit = GET_UNIT(channel[i]);
        uint8_t ch = channel[i] & 0x7;
        adc_pattern[i].atten = ADC_ATTEN_DB_11;
        adc_pattern[i].channel = ch;
        adc_pattern[i].unit = unit;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%x", i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%x", i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%x", i, adc_pattern[i].unit);
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_digi_controller_configure(&dig_cfg));
}

void setP1Axes(int index, int value) {
    switch (index) {
        case 0:
            Joystick.setXAxis(value);
            Joystick.setYAxis(0);
            break;
        case 1:
            Joystick.setXAxis(-value);
            Joystick.setYAxis(0);
            break;
        case 2:
            Joystick.setXAxis(0);
            Joystick.setYAxis(value);
            break;
        case 3:
            Joystick.setXAxis(0);
            Joystick.setYAxis(-value);
            break;
        default:
            Joystick.setXAxis(0);
            Joystick.setYAxis(0);
    }
}

void setP2Axes(int index, int value) {
    switch (index) {
        case 0:
            Joystick.setRxAxis(value);
            Joystick.setRyAxis(0);
            break;
        case 1:
            Joystick.setRxAxis(-value);
            Joystick.setRyAxis(0);
            break;
        case 2:
            Joystick.setRxAxis(0);
            Joystick.setRyAxis(value);
            break;
        case 3:
            Joystick.setRxAxis(0);
            Joystick.setRyAxis(-value);
            break;
        default:
            Joystick.setXAxis(0);
            Joystick.setYAxis(0);
    }
}

void setup() {
    for (byte p = 0; p < PLAYERS; p++) {
        for (byte i = 0; i < CHANNELS; i++) {
            power[p][i] = 0;
            pinMode(inPins[p][i], INPUT);
        }
    }
    USB.PID(0x4869);
    USB.VID(0x4869);
    USB.productName("Taiko Controller");
    USB.manufacturerName("GitHub Community");
    USB.begin();
    Joystick.begin(false);
    Joystick.setXAxisRange(-AXIS_RANGE, AXIS_RANGE);
    Joystick.setYAxisRange(-AXIS_RANGE, AXIS_RANGE);
    Joystick.setRxAxisRange(-AXIS_RANGE, AXIS_RANGE);
    Joystick.setRyAxisRange(-AXIS_RANGE, AXIS_RANGE);

    
}

void loop() {
    for (byte p = 0; p < PLAYERS; p++) {
        for (byte i = 0; i < CHANNELS; i++) {
            inputWindow[p][i].put(analogRead(inPins[p][i]));
        }
    }

    for (byte p = 0; p < PLAYERS; p++) {
        maxVal[p] = 0;
        maxIndex[p] = -1;

        for (byte i = 0; i < CHANNELS; i++) {
            power[p][i] = power[p][i] - inputWindow[p][i].get(1) +
                          inputWindow[p][i].get();
            float v = power[p][i] * sensitivities[p][i];
            axisValues[p][i] =
                AXIS_RANGE * (v >= MAX_THRES ? 1 : (v / MAX_THRES));
            if (axisValues[p][i] > maxVal[p]) {
                maxVal[p] = axisValues[p][i];
                maxIndex[p] = i;
            }
        }

        if (maxIndex[p] >= 0) {
            if (p == 0) {
                setP1Axes(maxIndex[p], maxVal[p]);
            } else if (p == 1) {
                setP2Axes(maxIndex[p], maxVal[p]);
            }
        } else {
            if (p == 0) {
                setP1Axes(-1, 0);
            } else if (p == 1) {
                setP2Axes(-1, 0);
            }
        }
    }

    Joystick.sendState();
}
