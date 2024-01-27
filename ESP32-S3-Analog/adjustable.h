#include "params.h"

const byte inPins[CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN
};
const byte sensitivityPins[CHANNELS] = {
    P1_L_DON_SENS, P1_L_KAT_SENS, P1_R_DON_SENS, P1_R_KAT_SENS
};

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];

#ifndef RAW_ANALOG_MODE
unsigned long lastPower[PLAYERS][CHANNELS];
bool triggered[PLAYERS];
unsigned long triggeredTime[PLAYERS][CHANNELS];
int outputValue[PLAYERS] = {0, 0};
uint resetTimer[PLAYERS] = {0, 0};
short maxIndex[PLAYERS] = {0, 0};
float maxPower[PLAYERS] = {0, 0};
#endif

uint axisValues[CHANNELS] = {0, 0, 0, 0};

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, 
                   false, false, false, false, false);

void setup() {
    for (byte i = 0; i < CHANNELS; i++) {
        power[i] = 0;
#ifndef RAW_ANALOG_MODE
        lastPower[i] = 0;
        triggered = false;
#endif
        pinMode(inPins[i], INPUT);
        pinMode(sensitivityPins[i], INPUT);
    }
#ifndef RAW_ANALOG_MODE
        maxIndex = -1;
        maxPower = 0;
#endif
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

    for (byte i = 0; i < CHANNELS; i++) {
        inputWindow[i].put(analogRead(inPins[i]));
        power[i] =  power[i] - inputWindow[i].get(1) + inputWindow[i].get();
#ifndef RAW_ANALOG_MODE
        if (lastPower[i] > maxPower && power[i] < lastPower[i]) {
            maxPower = lastPower[i];
            maxIndex = i;
        }
        lastPower[i] = power[i];
#else
        float x = analogRead(sensitivityPins[i]) / 2048.0 - 1;
        float x2 = x * x;
        float x3 = x2 * x;
        float x4 = x3 * x;
        float v = (1.0 + x + 0.5 * x2 + 0.166667 * x3) * power[i];
        axisValues[i] = AXIS_RANGE * (v >= MAX_THRES ? 1 : (v / MAX_THRES));
#endif
    }
#ifndef RAW_ANALOG_MODE
    if (!triggered && maxPower >= HIT_THRES) {
        triggered = true;
        digitalWrite(outPins[maxIndex], HIGH);
        outputValue = (int)(AXIS_RANGE * (maxPower >= MAX_THRES ? 1 : maxPower / MAX_THRES));
    }

    if (triggered && resetTimer >= RESET_TIME) {
        triggered = false;
        resetTimer = 0;
        digitalWrite(outPins[maxIndex], LOW);
        maxPower = 0;
        maxIndex = -1;
        outputValue = 0;
    }

    for (byte i = 0; i < CHANNELS; i++) {
        if (triggered && i == maxIndex) {
            axisValues[i] = outputValue;
        } else {
            axisValues[i] = 0;
        }
    }

    if (triggered) {
        resetTimer++;
    }
#endif

#if PLAYER_SELECT == 1
    Joystick.setXAxis(axisValues[0] > axisValues[1] ? axisValues[0] : -axisValues[1]);
    Joystick.setYAxis(axisValues[2] > axisValues[3] ? axisValues[2] : -axisValues[3]);
    Joystick.setRxAxis(0);
    Joystick.setRyAxis(0);
#elif PLAYER_SELECT == 2
    Joystick.setXAxis(0);
    Joystick.setYAxis(0);
    Joystick.setRxAxis(axisValues[0] > axisValues[1] ? axisValues[0] : -axisValues[1]);
    Joystick.setRyAxis(axisValues[2] > axisValues[3] ? axisValues[2] : -axisValues[3]);
#else
    Joystick.setXAxis(0);
    Joystick.setYAxis(0);
    Joystick.setRxAxis(0);
    Joystick.setRyAxis(0);
#endif

    Joystick.sendState();
}
