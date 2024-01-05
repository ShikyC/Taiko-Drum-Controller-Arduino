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
#define L_DON_SENS 1.0
#define L_KAT_SENS 1.0
#define R_DON_SENS 1.0
#define R_KAT_SENS 1.0

// Input pins for each channel
#define L_DON_IN 4
#define L_KAT_IN 5
#define R_DON_IN 6
#define R_KAT_IN 7

// Output LED pins for each channel (just for visualization)
#define L_DON_LED 10
#define L_KAT_LED 11
#define R_DON_LED 12
#define R_KAT_LED 13

#include "USB.h"
#include "Joystick_ESP32S2.h"
#include "cache.h"

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];
unsigned long lastPower[CHANNELS];

bool triggered;
unsigned long triggeredTime[CHANNELS];

const byte inPins[] = {L_DON_IN, L_KAT_IN, R_DON_IN, R_KAT_IN};
const byte outPins[] = {L_DON_LED, L_KAT_LED, R_DON_LED, R_KAT_LED};
const float sensitivities[] = {L_DON_SENS, L_KAT_SENS, R_DON_SENS, R_KAT_SENS};
uint axisValues[] = {0, 0, 0, 0};

uint shifter = 0;
int outputValue = 0;
uint resetTimer = 0;

short maxIndex;
float maxPower;

unsigned long lastTime;

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD, 10, 4,
                   true, true, true, true, true, true, 
                   false, false, false, false, false);

void setup() {
    Serial.begin(250000);
    for (byte i = 0; i < CHANNELS; i++) {
        power[i] = 0;
        lastPower[i] = 0;
        triggered = false;
        pinMode(inPins[i], INPUT);
        pinMode(outPins[i], OUTPUT);
    }
    maxIndex = -1;
    maxPower = 0;
    lastTime = micros();
    USB.PID(0x4869);
    USB.VID(0x4869);
    USB.productName("Taiko Controller");
    USB.manufacturerName("GitHub Community");
    USB.begin();
    Joystick.begin(false);
    Joystick.setXAxisRange(-1024, 1023);
    Joystick.setYAxisRange(-1024, 1023);
    Joystick.setZAxisRange(-1024, 1023);
    Joystick.setRxAxisRange(-1024, 1023);
    Joystick.setRyAxisRange(-1024, 1023);
    Joystick.setRzAxisRange(-1024, 1023);
}

void loop() {

    for (byte i = 0; i < CHANNELS; i++) {
        inputWindow[i].put(analogRead(inPins[i]));
        power[i] = sensitivities[i] * (power[i] - inputWindow[i].get(1) + inputWindow[i].get());

        if (lastPower[i] > maxPower && power[i] < lastPower[i]) {
            maxPower = lastPower[i];
            maxIndex = i;
        }
        lastPower[i] = power[i];
    }

    if (!triggered && maxPower >= HIT_THRES) {
        triggered = true;
        digitalWrite(outPins[maxIndex], HIGH);
        outputValue = (int)(1023 * (maxPower >= MAX_THRES ? 1 : maxPower / MAX_THRES));
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

    Joystick.setXAxis(axisValues[0]);
    Joystick.setYAxis(axisValues[1]);
    Joystick.setRxAxis(axisValues[2]);
    Joystick.setRyAxis(axisValues[3]);
    Joystick.sendState();

    if (triggered) {
        resetTimer++;
    }
}
