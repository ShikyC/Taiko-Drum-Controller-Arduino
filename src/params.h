// Enable this mode to pass raw analog data to the game without any post-
// processing.
// The game has a built-in mechanism to calculate which sensor is triggered
// and the force of the hit, so it's recommended to enable this mode.
// This also the provides the most similar experience with the arcade.
// To disable this mode, remove or comment out this line
#define RAW_ANALOG_MODE

// For MODE_ADJUSTABLE
// Select which player this board is. 1 for P1 and 2 for P2.
#define PLAYER_SELECT 1

// For MODE_TWO_PLAYERS
// Sensitivity multipliers for each channel, 1.0 as the baseline.
#define P1_L_DON_SENS 1.0
#define P1_L_KAT_SENS 1.0
#define P1_R_DON_SENS 1.0
#define P1_R_KAT_SENS 1.0
#define P2_L_DON_SENS 1.0
#define P2_L_KAT_SENS 1.0
#define P2_R_DON_SENS 1.0
#define P2_R_KAT_SENS 1.0

/**********************************************
  CHANGING THE FOLLOWING PARAMETERS ARE NOT
  RECOMMENDED UNLESS YOU KNOW HOW THEY WORK
***********************************************/

// Cache length must be the power of 2 (8, 16, 32, etc.).
// See cache.h for the reason.
#define SAMPLE_CACHE_LENGTH 32

// The maximum value of a hit (not the minumum value to trigger a heavy hit)
// To configure the light and heavy thresholds, do it in the game settings.
#define MAX_THRES 5000

// Input pins for each channel.
#define P1_L_DON_IN 4
#define P1_L_KAT_IN 5
#define P1_R_DON_IN 6
#define P1_R_KAT_IN 7

#define P2_L_DON_IN 8
#define P2_L_KAT_IN 1
#define P2_R_DON_IN 9
#define P2_R_KAT_IN 10

// Sensitivity adjustment potentiometer input pins.
#define P1_L_DON_SENS_IN 15
#define P1_L_KAT_SENS_IN 16
#define P1_R_DON_SENS_IN 17
#define P1_R_KAT_SENS_IN 18

#define AXIS_RANGE 1023

#define PLAYERS 2
#define CHANNELS 4

// The minimum value to trigger a light hit.
// Disabled if RAW_ANALOG_MODE is on.
#define HIT_THRES 1000

// If the reset time is too short, the game may not be able to 
// receive the input. From testing I found 40 seems to be the
// minimum value so that the game won't miss any hit. If the game
// occassionally miss the drum input, increase this value.
// Disabled if RAW_ANALOG_MODE is on.
#define RESET_TIME 40

#include <USB.h>
#include "joystick.h"
#include "cache.h"