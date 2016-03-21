/***************************************************************
 *                                                             *
 *                Taiko Sanro - Arduino                        *
 *   Support Arduino models with ATmega32u4 microprocessors    *
 *                                                             *
 *             Copyright © 2016 Shiky Chang                    *
 *                zhangxunpx@gmail.com                         *
 *                                                             *
 ***************************************************************/

#define MODE_JIRO 0

#define CHANNELS 4

// Input delay (in seconds) = (SAMPLE_CACHE_LENGTH + POWER_CACHE_LENGTH) / sample frequency
// _CACHE_LENGTH must be less than 256
#define SAMPLE_CACHE_LENGTH 150
#define POWER_CACHE_LENGTH 5

// _THRES must be less than 2^32 = 4294967296 = 4.29e9
#define LIGHT_THRES 3000000
#define HEAVY_THRES 10000000

#include <Keyboard.h>

int channelSample [CHANNELS];
int sampleCache [CHANNELS][SAMPLE_CACHE_LENGTH];
short int sampleCacheIndex [CHANNELS];

long int power [CHANNELS];
long int powerCache [CHANNELS][POWER_CACHE_LENGTH];
short int powerCacheIndex [CHANNELS];

bool triggered [CHANNELS];

void setup() {
  Serial.begin (9600);
  Keyboard.begin ();
  for (short int i = 0; i < CHANNELS; i++) {
    for (short int j = 0; j < SAMPLE_CACHE_LENGTH; j++) {
      sampleCache [i][j] = 0;
    }
    sampleCacheIndex [i] = SAMPLE_CACHE_LENGTH - 1; 
    for (short int j = 0; j < POWER_CACHE_LENGTH; j++) {
      powerCache [i][j] = 0;
    }
    powerCacheIndex [i] = POWER_CACHE_LENGTH - 1; 
    power [i] = 0;
    triggered [i] = false;
  }
}

void loop() {

// Analog input: 0~5V -> 0~1023
  channelSample[0] = analogRead (A0);  // L don
  channelSample[1] = analogRead (A1);  // R don
  channelSample[2] = analogRead (A2);  // L kat
  channelSample[3] = analogRead (A3);  // R kat

// Cache
// Start status: [ 0 ] [ 1 ] [ 2 ] ... [n-3] [n-2] [n-1]
//                 |                                 |
//               Oldest                           Pointer
//
//   New status: [NEW] [ 1 ] [ 2 ] ... [n-3] [n-2] [n-1]
//    (Loop 1)     |     |
//              Pointer  |
//                     Oldest
  for (short int i = 0; i < CHANNELS; i++) {
    sampleCacheIndex [i] ++;
    sampleCacheIndex [i] %= SAMPLE_CACHE_LENGTH;
    sampleCache [i][sampleCacheIndex [i]] = channelSample [i];
    power [i] += channelSample [i] * channelSample [i];
    int oldest = sampleCache [i][(sampleCacheIndex [i] + 1) % SAMPLE_CACHE_LENGTH];
    power [i] -= oldest * oldest;
    if (power [i] < LIGHT_THRES) {
      power [i] = 0;
      triggered [i] = false;
    }
    
    powerCacheIndex [i] ++;
    powerCacheIndex [i] %= POWER_CACHE_LENGTH;
    powerCache [i][powerCacheIndex [i]] = power [i];

// In the power cache, if we see any drop, we regard this point as a maximum
    for (short int j = 0; j < POWER_CACHE_LENGTH; j++){
      if (powerCache [i][(powerCacheIndex [i] + j) % POWER_CACHE_LENGTH] < powerCache [i][(powerCacheIndex [i] + j - 1) % POWER_CACHE_LENGTH]) {
#if MODE_JIRO
        if (power [i] >= LIGHT_THRES && triggered [i] == false) {
          triggered [i] = true;
          switch (i) {
            case 0: Keyboard.print ('g'); break;
            case 1: Keyboard.print ('h'); break;
            case 2: Keyboard.print ('f'); break;
            case 3: Keyboard.print ('j'); break;
          }
        }
#else
        if (power [i] >= HEAVY_THRES && triggered [i] == false) {
          triggered [i] = true;
          switch (i) {
            case 0: Keyboard.print ('t'); break;
            case 1: Keyboard.print ('y'); break;
            case 2: Keyboard.print ('r'); break;
            case 3: Keyboard.print ('u'); break;
          }
        } else if (power [i] >= LIGHT_THRES && triggered [i] == false) {
          triggered [i] = true;
          switch (i) {
            case 0: Keyboard.print ('g'); break;
            case 1: Keyboard.print ('h'); break;
            case 2: Keyboard.print ('f'); break;
            case 3: Keyboard.print ('j'); break;
          }
        }
#endif
        break;
      } else if (j == POWER_CACHE_LENGTH - 1) {
        // The power is still increasing
      }
    }

// This is the end of each channel
  }
  
  delay (1);
}

