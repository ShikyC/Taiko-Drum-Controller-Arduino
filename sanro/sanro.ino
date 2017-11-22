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

#define SAMPLE_CACHE_LENGTH 12
#define POWER_CACHE_LENGTH 3

#define LIGHT_THRES 8000
#define HEAVY_THRES 20000

#define FORCED_FREQ 1000

#include "cache.h"

unsigned long int lastTime;

float channelSample [CHANNELS];
float lastChannelSample [CHANNELS];
Cache <float, SAMPLE_CACHE_LENGTH> sampleCache [CHANNELS];

float power [CHANNELS];
Cache <float, POWER_CACHE_LENGTH> powerCache [CHANNELS];

bool triggered [CHANNELS];

int input_pins[] = {A0, A1}; // Don, Kat
int output_pins[] = {A2, A3, A4, A5}; // Left Don, Left Kat, Right Don, Right Kat

void setup() {
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, OUTPUT);
  pinMode(A3, OUTPUT);
  pinMode(A4, OUTPUT);
  pinMode(A5, OUTPUT);
  Serial.begin (9600);
  analogReference (INTERNAL);
  for (short int i = 0; i < CHANNELS; i++) {
    power [i] = 0;
    lastChannelSample [i] = 0;
    triggered [i] = false;
  }
  lastTime = 0;
}

void loop() {
  
  for (short int i = 0; i < CHANNELS; i++) {
    
    channelSample[i] = analogRead(input_pins[i]);
    sampleCache[i].put(channelSample [i]);
    
    long int tempInt;
    tempInt = sampleCache[i].get(1);
    power[i] -= tempInt * tempInt;
    tempInt = sampleCache[i].get();
    power[i] += tempInt * tempInt;
    
    powerCache[i].put(power[i]);
    lastChannelSample[i] = channelSample[i];
    if (powerCache[i].get(1) < LIGHT_THRES) {
      triggered[i] = false;
      digitalWrite(output_pins[i], HIGH);
      digitalWrite(output_pins[i] + 2, HIGH);
      digitalWrite(LED_BUILTIN, LOW);
    }

    if (!triggered [i]) {
      for (short int j = 0; j < POWER_CACHE_LENGTH - 1; j++) {
        if (powerCache [i].get (j - 1) >= powerCache [i].get (j)) {
          break;
        } else if (powerCache [i].get (1) >= HEAVY_THRES) {
          triggered [i] = true;
          digitalWrite(output_pins[i], LOW);
          digitalWrite(output_pins[i] + 2, LOW);
          digitalWrite(LED_BUILTIN, HIGH);
        } else if (powerCache [i].get (1) >= LIGHT_THRES) {
          triggered [i] = true;
          digitalWrite(output_pins[i], LOW);
          digitalWrite(LED_BUILTIN, HIGH);
        }
      }
    }

    unsigned int frameTime = micros () - lastTime;
    lastTime = micros ();
    if (frameTime < FORCED_FREQ) {
      delayMicroseconds (FORCED_FREQ - frameTime);
    }
  }
}
