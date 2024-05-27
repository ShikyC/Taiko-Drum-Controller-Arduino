#include "params.h"

// Select which player this board is. 1 for P1 and 2 for P2.
#define PLAYER_SELECT 1

const byte inPins[CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN
};
const byte sensitivityPins[CHANNELS] = {
    P1_L_DON_SENS_IN, P1_L_KAT_SENS_IN, P1_R_DON_SENS_IN, P1_R_KAT_SENS_IN
};

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];

uint axisValues[CHANNELS] = {0, 0, 0, 0};

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, 
                   false, false, false, false, false);

void setup() {
    for (byte i = 0; i < CHANNELS; i++) {
        power[i] = 0;
        pinMode(inPins[i], INPUT);
        pinMode(sensitivityPins[i], INPUT);
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

    for (byte i = 0; i < CHANNELS; i++) {
        inputWindow[i].put(analogRead(inPins[i]));
        power[i] =  power[i] - inputWindow[i].get(1) + inputWindow[i].get();
        float x = analogRead(sensitivityPins[i]) / 2048.0 - 1;
        float x2 = x * x;
        float x3 = x2 * x;
        float x4 = x3 * x;
        float v = (1.0 + x + 0.5 * x2 + 0.166667 * x3) * power[i];
        axisValues[i] = AXIS_RANGE * (v >= MAX_THRES ? 1 : (v / MAX_THRES));
    }

#if PLAYER_SELECT == 1
    Joystick.setXAxis(axisValues[0] > axisValues[1] ? axisValues[0] : -axisValues[1]);
    Joystick.setYAxis(axisValues[2] > axisValues[3] ? axisValues[2] : -axisValues[3]);
#elif PLAYER_SELECT == 2
    Joystick.setRxAxis(axisValues[0] > axisValues[1] ? axisValues[0] : -axisValues[1]);
    Joystick.setRyAxis(axisValues[2] > axisValues[3] ? axisValues[2] : -axisValues[3]);
#endif

    Joystick.sendState();
}
