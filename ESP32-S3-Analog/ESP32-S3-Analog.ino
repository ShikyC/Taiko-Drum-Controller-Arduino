// Enable this mode to pass raw analog data to the game without any post-
// processing.
// The game has a built-in mechanism to calculate which sensor is triggered
// and the force of the hit, so it's recommended to enable this mode.
// This also the provides the most similar experience with the arcade.
// If turned on, the microcontroller will ony do a fast sigle-pass convolution
// over the piezoelectric sensors' inputs, and then pass the data to the game
// directly.
#define RAW_ANALOG_MODE 1

#define PLAYERS 2
#define CHANNELS 4

// SAMPLE_CACHE_LENGTH must be power of 2 (8, 16, 32, etc.)
// See cache.h for implementation
#define SAMPLE_CACHE_LENGTH 16

// The maximum value of a hit (not the minumum value to trigger a heavy hit)
// To configure the light and heavy thresholds, do it in the game settings
#define MAX_THRES 5000

#if !RAW_ANALOG_MODE
// The minimum value to trigger a light hit
#define HIT_THRES 1000

// If the reset time is too short, the game may not be able to 
// receive the input. From testing I found 40 seems to be the
// minimum value so that the game won't miss any hit. If the game
// occassionally miss the drum input, increase this value
#define RESET_TIME 40
#endif

// Sensitivity multipliers for each channel, 1.0 as the baseline
#define P1_L_DON_SENS 1.0
#define P1_L_KAT_SENS 1.0
#define P1_R_DON_SENS 1.0
#define P1_R_KAT_SENS 1.0
#define P2_L_DON_SENS 1.0
#define P2_L_KAT_SENS 1.0
#define P2_R_DON_SENS 1.0
#define P2_R_KAT_SENS 1.0

// Input pins for each channel
#define P1_L_DON_IN 4
#define P1_L_KAT_IN 5
#define P1_R_DON_IN 6
#define P1_R_KAT_IN 7
#define P2_L_DON_IN 8
#define P2_L_KAT_IN 1
#define P2_R_DON_IN 9
#define P2_R_KAT_IN 10

#define AXIS_RANGE 1023

#include "USB.h"
#include "Joystick_ESP32S2.h"
#include "cache.h"

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

#if !RAW_ANALOG_MODE
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
#if !RAW_ANALOG_MODE
            lastPower[p][i] = 0;
            triggered[p] = false;
#endif
            pinMode(inPins[p][i], INPUT);
        }
#if !RAW_ANALOG_MODE
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
#if !RAW_ANALOG_MODE
            if (lastPower[p][i] > maxPower[p] && power[p][i] < lastPower[p][i]) {
                maxPower[p] = lastPower[p][i];
                maxIndex[p] = i;
            }
            lastPower[p][i] = power[p][i];
#else
            float v = pow(5.0, sensitivities[p][i] / 2048.0 - 1) * power[p][i];
            axisValues[p][i] = AXIS_RANGE * (v >= MAX_THRES ? 1 : (v / MAX_THRES));
#endif
        }
#if !RAW_ANALOG_MODE
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
