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

#define MODE_DEBUG 0

#define CHANNELS 2

// Caches for the soundwave and power
#define SAMPLE_CACHE_LENGTH 12
#define POWER_CACHE_LENGTH 3

// Light and heacy hit thresholds
#define LIGHT_THRES 5000
#define HEAVY_THRES 20000

// Forced sampling frequency
#define FORCED_FREQ 1000

#include "cache.h"

unsigned long int lastTime;

int channelSample [CHANNELS];
int lastChannelSample [CHANNELS];
Cache <int, SAMPLE_CACHE_LENGTH> sampleCache [CHANNELS];

long int power [CHANNELS];
Cache <long int, POWER_CACHE_LENGTH> powerCache [CHANNELS];

bool triggered [CHANNELS];

int pins[] = {A0, A1}; // Don, Kat

void setup() {
  Serial.begin (9600);
  Keyboard.begin ();
  analogReference (DEFAULT);
  for (short int i = 0; i < CHANNELS; i++) {
    power [i] = 0;
    lastChannelSample [i] = 0;
    triggered [i] = false;
  }
  lastTime = 0;
}

void loop() {
  
  for (short int i = 0; i < CHANNELS; i++) {
    
    channelSample[i] = analogRead (pins [i]);
    sampleCache [i].put (channelSample [i] - lastChannelSample [i]);
    
    long int tempInt;
    tempInt = sampleCache [i].get (1);
    power [i] -= tempInt * tempInt;
    tempInt = sampleCache [i].get ();
    power [i] += tempInt * tempInt;
    if (power [i] < LIGHT_THRES) {
      power [i] = 0;
    }
    
    powerCache [i].put (power [i]);
    lastChannelSample [i] = channelSample [i];
    if (powerCache [i].get (1) == 0) {
      triggered [i] = false;
    }

    if (!triggered [i]) {
      for (short int j = 0; j < POWER_CACHE_LENGTH - 1; j++) {
        if (powerCache [i].get (j - 1) >= powerCache [i].get (j)) {
          break;
        } else if (powerCache [i].get (1) >= HEAVY_THRES) {
          triggered [i] = true;
          Keyboard.print (heavyKeys [i]);
        } else if (powerCache [i].get (1) >= LIGHT_THRES) {
          triggered [i] = true;
          Keyboard.print (lightKeys [i]);
        }
      }
    }
    
#if MODE_DEBUG
    Serial.print (power [i]);
    Serial.print ("\t");
#endif

// End of each channel
  }

#if MODE_DEBUG
  Serial.print (50000);
  Serial.print ("\t");
  Serial.print (0);
  Serial.print ("\t");

  Serial.println ("");
#endif

// Force the sample frequency to be less than 1000Hz
  unsigned int frameTime = micros () - lastTime;
  lastTime = micros ();
  if (frameTime < FORCED_FREQ) {
    delayMicroseconds (FORCED_FREQ - frameTime);
  } else {
    // Performance bottleneck;
    Serial.print ("Exception: forced frequency is too high for the microprocessor to catch up.");
  }
}
