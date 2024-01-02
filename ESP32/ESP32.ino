#define CHANNELS 4

// SAMPLE_CACHE_LENGTH must be power of 2 (8, 16, 32, etc.)
// See cache.h for implementation
#define SAMPLE_CACHE_LENGTH 16

// The thresholds are also dependent on SAMPLE_CACHE_LENGTH, if you
// changed SAMPLE_CACHE_LENGTH, you should also adjust thresholds
#define HIT_THRES 750
#define RESET_THRES 200

// Sampling period in μs, e.g., 500μs = 0.5ms = 2000Hz
#define SAMPLING_PERIOD 500

// Sensitivity multipliers for each channel, 1.0 as the baseline
#define L_DON_SENS 1.0
#define L_KAT_SENS 1.0
#define R_DON_SENS 1.0
#define R_KAT_SENS 1.0

// Input pins for each channel
#define L_DON_IN 36
#define L_KAT_IN 39
#define R_DON_IN 34
#define R_KAT_IN 35

// Output LED pins for each channel (just for visualization)
#define L_DON_LED 25
#define L_KAT_LED 26
#define R_DON_LED 27
#define R_KAT_LED 14

// Keyboard output for each channel
#define L_DON_KEY 'f'
#define L_KAT_KEY 'd'
#define R_DON_KEY 'j'
#define R_KAT_KEY 'k'

// Enable debug mode to view analog input values from the Serial
// Enabling this also disables the keyboard simulation
#define DEBUG 0

#include "cache.h"
#include "keyboard.h"

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];
unsigned long lastPower[CHANNELS];

bool triggered;
unsigned long triggeredTime[CHANNELS];

const byte inPins[] = {L_DON_IN, L_KAT_IN, R_DON_IN, R_KAT_IN};
const byte outPins[] = {L_DON_LED, L_KAT_LED, R_DON_LED, R_KAT_LED};
const char outKeys[] = {L_DON_KEY, L_KAT_KEY, R_DON_KEY, R_KAT_KEY};
float sensitivities[] = {L_DON_SENS, L_KAT_SENS, R_DON_SENS, R_KAT_SENS};

short maxIndex;
float maxPower;

unsigned long lastTime;

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
    xTaskCreate(bluetoothTask, "bluetooth", 20000, NULL, 5, NULL);
    lastTime = micros();
}

void loop() {
    if (maxIndex != -1 && lastPower[maxIndex] < RESET_THRES) {
        triggered = false;
        digitalWrite(outPins[maxIndex], LOW);
        maxIndex = -1;
        maxPower = 0;
    }

    for (byte i = 0; i < CHANNELS; i++) {
        inputWindow[i].put(analogRead(inPins[i]));
        power[i] = sensitivities[i] *
                   (power[i] - inputWindow[i].get(1) + inputWindow[i].get());

        if (lastPower[i] > maxPower && power[i] < lastPower[i]) {
            maxPower = lastPower[i];
            maxIndex = i;
        }
        lastPower[i] = power[i];
#if DEBUG
        Serial.print(power[i]);
        Serial.print(" ");
#endif
    }

    if (!triggered && maxPower >= HIT_THRES) {
        triggered = true;
        digitalWrite(outPins[maxIndex], HIGH);
#if !DEBUG
        if (isBleConnected) {
            typeChar(outKeys[maxIndex]);
        }
#endif
    }
#if DEBUG
    Serial.print("\n");
#endif

    unsigned int frameTime = micros() - lastTime;
    if (frameTime < SAMPLING_PERIOD) {
        delayMicroseconds(SAMPLING_PERIOD - frameTime);
    }
    lastTime = micros();
}
