/***************************************************************
 *                                                             *
 *                Taiko Sanro - Arduino                        *
 *   Support Arduino models with ATmega32u4 microprocessors    *
 *                                                             *
 *          Shiky Chang                    Chris               *
 *     zhangxunpx@gmail.com          wisaly@gmail.com          *
 *                                                             *
 ***************************************************************/

#define MODE_JIRO 1
#define MODE_DEBUG 0

#define CHANNELS 4

#define SAMPLE_CACHE_LENGTH 12
#define POWER_CACHE_LENGTH 3

#define LIGHT_THRES 5000
#define HEAVY_THRES 20000

#include <limits.h>
#include <Keyboard.h>
#include "cache.h"

#if MODE_JIRO
#define HEAVY_THRES LONG_MAX
#endif

unsigned long int lastTime;

int channelSample [CHANNELS];
int lastChannelSample [CHANNELS];
Cache <int, SAMPLE_CACHE_LENGTH> sampleCache [CHANNELS];

long int power [CHANNELS];
Cache <long int, POWER_CACHE_LENGTH> powerCache [CHANNELS];

bool triggered [CHANNELS];

int pins[] = {A0, A1, A2, A3};  // L don, R don, L kat, R kat
char lightKeys[] = {'g', 'h', 'f', 'j'};
char heavyKeys[] = {'t', 'y', 'r', 'u'};

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
    
#if MODE_DEBUGjj
    Serial.print (power [i]);
    Serial.print ("\t");
#endif

// End of each channel
  }

#if MODE_DEBUG
// Damn Arduino plotter, the Y axis scale changes too frequently! Use these values to "lock" the plotter's scale.
  Serial.print (50000);
  Serial.print ("\t");
  Serial.print (0);
  Serial.print ("\t");

  Serial.println ("");
#endif

// Force the sample frequency to be less than 1000Hz
  unsigned int frameTime = micros () - lastTime;
  lastTime = micros ();
  if (frameTime < 1000) {
    delayMicroseconds (1000 - frameTime);
  }
}
