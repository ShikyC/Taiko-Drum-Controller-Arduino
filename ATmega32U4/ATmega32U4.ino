#define CHANNELS 4
#define SAMPLE_CACHE_LENGTH 16 // Must be power of 2 (8, 16, etc.); See cache.h for implementation
#define HIT_THRES 750          // The thresholds are also dependent on SAMPLE_CACHE_LENGTH, if you
#define RESET_THRES 300        // changed SAMPLE_CACHE_LENGTH, you must adjust thresholds here

#define DEBUG 0

#include <Keyboard.h>
#include <limits.h>

#include "cache.h"

unsigned long int lastTime;

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];
unsigned long lastPower[CHANNELS];

bool triggered;
unsigned long triggeredTime[CHANNELS];

const byte inPins[] = {A0, A1, A2, A3};        // L don, L kat, R don, R kat
const byte outPins[] = {5, 6, 7, 8};           // LED visualization (optional)
const char outKeys[] = {'f', 'd', 'j', 'k'};   // L don, L kat, R don, R kat

float sensitivity[] = {1.0, 1.0, 1.0, 1.0};

short maxIndex;
float maxPower;

void setup() {
    Serial.begin(115200);
    Keyboard.begin();
    analogReference(DEFAULT);
    for (byte i = 0; i < CHANNELS; i++) {
        power[i] = 0;
        lastPower[i] = 0;
        triggered = false;
    }
    lastTime = 0;
    maxIndex = -1;
    maxPower = 0;
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
        Keyboard.print(outKeys[maxIndex]);
#endif
    }
#if DEBUG
    Serial.print("\n");
#endif
}
