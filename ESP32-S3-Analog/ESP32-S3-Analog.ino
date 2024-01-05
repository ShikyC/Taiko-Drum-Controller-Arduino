#define PLAYERS 2
#define CHANNELS 4

// SAMPLE_CACHE_LENGTH must be power of 2 (8, 16, 32, etc.)
// See cache.h for implementation
#define SAMPLE_CACHE_LENGTH 16

// The thresholds are also dependent on SAMPLE_CACHE_LENGTH, if you
// changed SAMPLE_CACHE_LENGTH, you should also adjust thresholds
#define MAX_THRES 5000
#define HIT_THRES 1000

// If the reset time is too short, the game may not be able to 
// receive the input. From testing I found 40 seems to be the
// minimum value so that the game won't miss any hit. If the game
// occassionally miss the drum input, increase this value
#define RESET_TIME 40

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
#define P2_L_KAT_IN 3
#define P2_R_DON_IN 9
#define P2_R_KAT_IN 10

// Output LED pins for each channel (just for visualization)
#define P1_L_DON_LED 11
#define P1_L_KAT_LED 12
#define P1_R_DON_LED 13
#define P1_R_KAT_LED 14
#define P2_L_DON_LED 42
#define P2_L_KAT_LED 41
#define P2_R_DON_LED 40
#define P2_R_KAT_LED 39

#define AXIS_RANGE 1023

#include "USB.h"
#include "Joystick_ESP32S2.h"
#include "cache.h"

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[PLAYERS][CHANNELS];
unsigned long power[PLAYERS][CHANNELS];
unsigned long lastPower[PLAYERS][CHANNELS];

bool triggered[PLAYERS];
unsigned long triggeredTime[PLAYERS][CHANNELS];

const byte inPins[PLAYERS][CHANNELS] = {
    P1_L_DON_IN, P1_L_KAT_IN, P1_R_DON_IN, P1_R_KAT_IN, 
    P2_L_DON_IN, P2_L_KAT_IN, P2_R_DON_IN, P2_R_KAT_IN
};
const byte outPins[PLAYERS][CHANNELS] = {
    P1_L_DON_LED, P1_L_KAT_LED, P1_R_DON_LED, P1_R_KAT_LED, 
    P2_L_DON_LED, P2_L_KAT_LED, P2_R_DON_LED, P2_R_KAT_LED
};
const float sensitivities[PLAYERS][CHANNELS] = {
    P1_L_DON_SENS, P1_L_KAT_SENS, P1_R_DON_SENS, P1_R_KAT_SENS, 
    P2_L_DON_SENS, P2_L_KAT_SENS, P2_R_DON_SENS, P2_R_KAT_SENS
};

uint axisValues[PLAYERS][CHANNELS] = {0, 0, 0, 0, 0, 0, 0, 0};

int outputValue[PLAYERS] = {0, 0};
uint resetTimer[PLAYERS] = {0, 0};

short maxIndex[PLAYERS] = {0, 0};
float maxPower[PLAYERS] = {0, 0};

uint shifter = 0;

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, false, true, true, false, 
                   false, false, false, false, false);

void setup() {
    for (byte p = 0; p < PLAYERS; p++) {
        for (byte i = 0; i < CHANNELS; i++) {
            power[p][i] = 0;
            lastPower[p][i] = 0;
            triggered[p] = false;
            pinMode(inPins[p][i], INPUT);
            pinMode(outPins[p][i], OUTPUT);
        }
        maxIndex[p] = -1;
        maxPower[p] = 0;
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
            power[p][i] = sensitivities[p][i] * (power[p][i] - inputWindow[p][i].get(1) + inputWindow[p][i].get());

            if (lastPower[p][i] > maxPower[p] && power[p][i] < lastPower[p][i]) {
                maxPower[p] = lastPower[p][i];
                maxIndex[p] = i;
            }
            lastPower[p][i] = power[p][i];
        }

        if (!triggered[p] && maxPower[p] >= HIT_THRES) {
            triggered[p] = true;
            digitalWrite(outPins[p][maxIndex[p]], HIGH);
            outputValue[p] = (int)(AXIS_RANGE * (maxPower[p] >= MAX_THRES ? 1 : maxPower[p] / MAX_THRES));
        }

        if (triggered[p] && resetTimer[p] >= RESET_TIME) {
            triggered[p] = false;
            resetTimer[p] = 0;
            digitalWrite(outPins[p][maxIndex[p]], LOW);
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
    }
    
    Joystick.setXAxis((axisValues[0][0] > 0 ? axisValues[0][0] : -axisValues[0][1]));
    Joystick.setYAxis((axisValues[0][2] > 0 ? axisValues[0][2] : -axisValues[0][3]));
    Joystick.setRxAxis((axisValues[1][0] > 0 ? axisValues[1][0] : -axisValues[1][1]));
    Joystick.setRyAxis((axisValues[1][2] > 0 ? axisValues[1][2] : -axisValues[1][3]));
    Joystick.sendState();
}
