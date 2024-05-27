// Sample window length. Larger values reduce noise but add more latency.
#define SAMPLE_CACHE_LENGTH 64

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

// Controller axis value range
#define AXIS_RANGE 1023

// Number of input channels for each player
#define CHANNELS 4

#include <USB.h>
#include "joystick.h"
#include "cache.h"