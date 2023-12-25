#define CHANNELS 4
#define SAMPLE_CACHE_LENGTH 32 // Must be power of 2 (8, 16, etc.); See cache.h for implementation
#define HIT_THRES 750          // The thresholds are also dependent on SAMPLE_CACHE_LENGTH, if you
#define RESET_THRES 300        // changed SAMPLE_CACHE_LENGTH, you must also adjust thresholds
#define SAMPLING_PERIOD 1000   // Sampling period in microseconds (us), 1000us = 1ms = 0.001s 

#define DEBUG 1

#include "cache.h"
#include "keyboard.h"

Cache<int, SAMPLE_CACHE_LENGTH> inputWindow[CHANNELS];
unsigned long power[CHANNELS];
unsigned long lastPower[CHANNELS];

bool triggered;
unsigned long triggeredTime[CHANNELS];

const byte inPins[] = {35, 34, 39, 36};        // L don, L kat, R don, R kat
const byte outPins[] = {25, 26, 27, 14};       // LED visualization (optional)
const char* outKeys[] = {"f", "d", "j", "k"};   // L don, L kat, R don, R kat

float sensitivity[] = {1.0, 1.0, 1.0, 1.0};

short maxIndex;
float maxPower;

void bluetoothTask(void*);
void typeText(const char* text);

unsigned long lastTime;

void setup() {
    Serial.begin(250000);
    for (byte i = 0; i < CHANNELS; i++) {
        power[i] = 0;
        lastPower[i] = 0;
        triggered = false;
        pinMode(outPins[i], OUTPUT);
    }
    lastTime = 0;
    maxIndex = -1;
    maxPower = 0;
    xTaskCreate(bluetoothTask, "bluetooth", 20000, NULL, 5, NULL);
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
            typeText(outKeys[maxIndex]);
        }
#endif
    }
#if DEBUG
    Serial.print("\n");
#endif

    // unsigned int frameTime = micros() - lastTime;
    // lastTime = micros();
    // if (frameTime < SAMPLING_PERIOD) {
    //     delayMicroseconds(SAMPLING_PERIOD - frameTime);
    // }
}
