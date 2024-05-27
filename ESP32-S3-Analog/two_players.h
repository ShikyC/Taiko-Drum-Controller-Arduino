#include "driver/i2s.h"
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

const byte players = 2;

const byte inPins[players][CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN, 
    P2_L_DON_IN, P2_L_KAT_IN, P2_R_DON_IN, P2_R_KAT_IN
};

const float sensitivities[players][CHANNELS] = {
    P1_L_DON_SENS, P1_L_KAT_SENS, P1_R_DON_SENS, P1_R_KAT_SENS,
    P2_L_DON_SENS, P2_L_KAT_SENS, P2_R_DON_SENS, P2_R_KAT_SENS};

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[players][CHANNELS];
unsigned long power[players][CHANNELS];

uint axisValues[players][CHANNELS] = {0, 0, 0, 0, 0, 0, 0, 0};

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, false, false, false,
                   false, false);

uint maxVal[players] = {0, 0};
uint maxIndex[players] = {-1, -1};

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
    for (byte p = 0; p < players; p++) {
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
    for (byte p = 0; p < players; p++) {
        for (byte i = 0; i < CHANNELS; i++) {
            inputWindow[p][i].put(analogRead(inPins[p][i]));
        }
    }

    for (byte p = 0; p < players; p++) {
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
