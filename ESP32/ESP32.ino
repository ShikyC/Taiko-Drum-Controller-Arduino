#define CHANNELS 4
#define SAMPLE_CACHE_LENGTH 16 // Must be power of 2 (8, 16, etc.); See cache.h for implementation
#define HIT_THRES 750          // The thresholds are also dependent on SAMPLE_CACHE_LENGTH, if you
#define RESET_THRES 100        // changed SAMPLE_CACHE_LENGTH, you must also adjust thresholds
#define SAMPLING_PERIOD 500    // Sampling period in microseconds, 500us = 0.5ms = 2000Hz

#define DEBUG 0

#include "cache.h"
#include "keyboard.h"

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];
unsigned long lastPower[CHANNELS];

bool triggered;
unsigned long triggeredTime[CHANNELS];

const byte inPins[] = {36, 39, 34, 35};        // L don, L kat, R don, R kat
const byte outPins[] = {25, 26, 27, 14};       // LED visualization (optional)
const char outKeys[] = {'f', 'd', 'j', 'k'};   // L don, L kat, R don, R kat

float sensitivity[] = {1.0, 1.0, 1.0, 1.0};

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
        power[i] = sensitivity[i] * (power[i] - inputWindow[i].get(1) + inputWindow[i].get());

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
