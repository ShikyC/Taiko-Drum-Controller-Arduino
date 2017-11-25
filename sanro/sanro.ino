/***************************************************************
 *                                                             *
 *                Taiko Sanro - Arduino                        *
 *   Support Arduino models with ATmega32u4 microprocessors    *
 *                                                             *
 *          Shiky Chang                    Chris               *
 *     zhangxunpx@gmail.com          wisaly@gmail.com          *
 *                                                             *
 ***************************************************************/

// New implementation using fast, stable and sensitive piezoelectric
// ceramic sensors (the same sensors used in electirc drum kit).
// No longer need microphones.

#define CHANNELS 2

#define SAMPLE_CACHE_LENGTH 10

#define DON_SILENCE_THRES 1e4
#define DON_LIGHT_THRES   3e4
#define DON_HEAVY_THRES   1e5

#define KAT_SILENCE_THRES 4e3
#define KAT_LIGHT_THRES   5e3
#define KAT_HEAVY_THRES   1e4

#define FORCED_FREQ 1000

#include "cache.h"

unsigned long lastTime;

float channelSample[CHANNELS];
Cache <float, SAMPLE_CACHE_LENGTH> sampleCache[CHANNELS];
float power[CHANNELS];

bool triggered;
float light_thres[] = {DON_LIGHT_THRES, KAT_LIGHT_THRES};
float heavy_thres[] = {DON_HEAVY_THRES, KAT_HEAVY_THRES};
float silence_thres[] = {DON_SILENCE_THRES, KAT_SILENCE_THRES};

int input_pins[] = {A0, A1}; // Don, Kat
int output_pins[] = {A2, A3, A4, A5}; // Left Don, Left Kat, Right Don, Right Kat

String debug_output[] = {"D", "K"};

void setup() {
  pinMode(A2, OUTPUT);
  pinMode(A3, OUTPUT);
  pinMode(A4, OUTPUT);
  pinMode(A5, OUTPUT);
  Serial.begin (115200);
  for (short int i = 0; i < CHANNELS; i++) {
    power [i] = 0; 
  }
  triggered = false;
  lastTime = 0;
}

void loop() {
  lastTime = micros ();
  for (short int i = 0; i < CHANNELS; i++) {
    channelSample[i] = analogRead(input_pins[i]);
    sampleCache[i].put(channelSample[i]);
    
    long int tempInt = sampleCache[i].get(1);
    power[i] -= tempInt * tempInt;
    tempInt = sampleCache[i].get();
    power[i] += tempInt * tempInt;

    if (power[i] > light_thres[i]) {
      if (!triggered) {
        digitalWrite(output_pins[i], LOW);
        if (power[i] >= heavy_thres[i]) {
          digitalWrite(output_pins[i] + 2, LOW);
        }
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println(debug_output[i]);
      }
      triggered = true;
    }
  }
  
  for (short int i = 0; i < CHANNELS; i++) {
    triggered = false;
    digitalWrite(output_pins[i], HIGH);
    digitalWrite(output_pins[i] + 2, HIGH);
    if (power[i] > silence_thres[i]) {
      digitalWrite(LED_BUILTIN, LOW);
      triggered = true;
      break;
    }
  }
  
//  Serial.println(power[0]);
  float delayTime = 1e6 / FORCED_FREQ - (micros() - lastTime);
  if (delayTime > 0) {
    longMicroDelay(delayTime);
  } else {
//    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void longMicroDelay (float microTime) {
  delay (microTime / 1000);
  delayMicroseconds (microTime - floor(microTime / 1000) * 1000);
}
