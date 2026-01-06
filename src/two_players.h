#include "params.h"

const byte inPins[PLAYERS][CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN, 
    P2_L_DON_IN, P2_L_KAT_IN, P2_R_DON_IN, P2_R_KAT_IN
};

const float sensitivities[PLAYERS][CHANNELS] = {
    P1_L_DON_SENS, P1_L_KAT_SENS, P1_R_DON_SENS, P1_R_KAT_SENS, 
    P2_L_DON_SENS, P2_L_KAT_SENS, P2_R_DON_SENS, P2_R_KAT_SENS
};

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[PLAYERS][CHANNELS];
unsigned long power[PLAYERS][CHANNELS];

#ifndef RAW_ANALOG_MODE
unsigned long lastPower[PLAYERS][CHANNELS];
bool triggered[PLAYERS];
unsigned long triggeredTime[PLAYERS][CHANNELS];
int outputValue[PLAYERS] = {0, 0};
uint resetTimer[PLAYERS] = {0, 0};
short maxIndex[PLAYERS] = {0, 0};
float maxPower[PLAYERS] = {0, 0};
#endif

uint axisValues[PLAYERS][CHANNELS] = {0, 0, 0, 0, 0, 0, 0, 0};

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, 
                   false, false, false, false, false);

void setup() {
    for (byte p = 0; p < PLAYERS; p++) {
        for (byte i = 0; i < CHANNELS; i++) {
            power[p][i] = 0;
#ifndef RAW_ANALOG_MODE
            lastPower[p][i] = 0;
            triggered[p] = false;
#endif
            pinMode(inPins[p][i], INPUT);
        }
#ifndef RAW_ANALOG_MODE
        maxIndex[p] = -1;
        maxPower[p] = 0;
#endif
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
            power[p][i] = power[p][i] - inputWindow[p][i].get(1) + inputWindow[p][i].get();
#ifndef RAW_ANALOG_MODE
            if (lastPower[p][i] > maxPower[p] && power[p][i] < lastPower[p][i]) {
                maxPower[p] = lastPower[p][i];
                maxIndex[p] = i;
            }
            lastPower[p][i] = power[p][i];
#else
            float v = power[p][i] * sensitivities[p][i];
            axisValues[p][i] = AXIS_RANGE * (v >= MAX_THRES ? 1 : (v / MAX_THRES));
#endif
        }
#ifndef RAW_ANALOG_MODE
        if (!triggered[p] && maxPower[p] >= HIT_THRES) {
            triggered[p] = true;
            outputValue[p] = (int)(AXIS_RANGE * (maxPower[p] >= MAX_THRES ? 1 : maxPower[p] / MAX_THRES));
        }

        if (triggered[p] && resetTimer[p] >= RESET_TIME) {
            triggered[p] = false;
            resetTimer[p] = 0;
            maxPower[p] = 0;
            maxIndex[p] = -1;
            outputValue[p] = 0;
        }

        for (byte i = 0; i < CHANNELS; i++) {
            if (triggered[p] && i == maxIndex[p]) {
                axisValues[p][i] = outputValue[p];
            } else {
                axisValues[p][i] = 0;
            }
        }

        if (triggered[p]) {
            resetTimer[p]++;
        }
#endif
    }

    Joystick.setXAxis(axisValues[0][0] > axisValues[0][1] ? axisValues[0][0] : -axisValues[0][1]);
    Joystick.setYAxis(axisValues[0][2] > axisValues[0][3] ? axisValues[0][2] : -axisValues[0][3]);
    Joystick.setRxAxis(axisValues[1][0] > axisValues[1][1] ? axisValues[1][0] : -axisValues[1][1]);
    Joystick.setRyAxis(axisValues[1][2] > axisValues[1][3] ? axisValues[1][2] : -axisValues[1][3]);
    Joystick.sendState();
}
