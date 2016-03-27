/***************************************************************
 *                                                             *
 *                Taiko Sanro - Arduino                        *
 *   Support Arduino models with ATmega32u4 microprocessors    *
 *                                                             *
 *                     Shiky Chang                             *
 *                zhangxunpx@gmail.com                         *
 *                                                             *
 ***************************************************************/

#define MODE_JIRO 1
#define MODE_DEBUG 1

#define CHANNELS 4

#define SAMPLE_CACHE_LENGTH 50
#define POWER_CACHE_LENGTH 3

#define LIGHT_THRES 0
#define HEAVY_THRES 70000

#include <Keyboard.h>
#include "cache.h"

int channelSample [CHANNELS];
int lastChannelSample [CHANNELS];
Cache <int, SAMPLE_CACHE_LENGTH> sampleCache [CHANNELS];

long int power [CHANNELS];
Cache <long int, POWER_CACHE_LENGTH> powerCache [CHANNELS];

bool triggered [CHANNELS];

#if MODE_DEBUG
long int lastTime;
#endif

void setup() {
  Serial.begin (9600);
  Keyboard.begin ();
  analogReference (DEFAULT);
  for (short int i = 0; i < CHANNELS; i++) {
    power [i] = 0;
    lastChannelSample [i] = 0;
    triggered [i] = false;
  }
#if MODE_DEBUG
lastTime = 0;
#endif
}

void loop() {

  channelSample[0] = analogRead (A0);  // L don
  channelSample[1] = analogRead (A1);  // R don
  channelSample[2] = analogRead (A2);  // L kat
  channelSample[3] = analogRead (A3);  // R kat

#if MODE_DEBUG
  Serial.print (micros () - lastTime);
  lastTime = micros ();
  Serial.print ("\t");
#endif
  
  for (short int i = 0; i < CHANNELS; i++) {
    
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
    
    for (short int j = 0; j < POWER_CACHE_LENGTH - 1; j++){
      
      if (powerCache [i].get (j) == 0) {
        triggered [i] = false;
      }
      
      if (!triggered [i]) {
        if (powerCache [i].get (j + 1) >= powerCache [i].get (j) || j != POWER_CACHE_LENGTH - 2) {
          break;
        } else {
#if MODE_JIRO
          if (power [i] >= LIGHT_THRES) {
            triggered [i] = true;
            switch (i) {
              case 0: Keyboard.print ('g'); break;
              case 1: Keyboard.print ('h'); break;
              case 2: Keyboard.print ('f'); break;
              case 3: Keyboard.print ('j'); break;
            }
          }
#else
          if (power [i] >= HEAVY_THRES) {
            triggered [i] = true;
            switch (i) {
              case 0: Keyboard.print ('t'); break;
              case 1: Keyboard.print ('y'); break;
              case 2: Keyboard.print ('r'); break;
              case 3: Keyboard.print ('u'); break;
            }
          } else if (power [i] >= LIGHT_THRES) {
            triggered [i] = true;
            switch (i) {
              case 0: Keyboard.print ('g'); break;
              case 1: Keyboard.print ('h'); break;
              case 2: Keyboard.print ('f'); break;
              case 3: Keyboard.print ('j'); break;
            }
          }
#endif
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
  Serial.print (micros () - lastTime);
  lastTime = micros ();
  Serial.println ("");
#endif

}

