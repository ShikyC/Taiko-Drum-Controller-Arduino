/********************************************************

ESP32 is not fast enough to process 16 channels of input
simultaneously, so you can choose one of the 2 modes:

- MODE_TWO_PLAYERS: Allows the controller to connect 2 
    drums, but the sensitivity values are flashed to the 
    board so you can't adjust them without re-flash the 
    firmware. If you use the provided board, connect the
    2 drums and ignore the adjustable resistors.

- MODE_ADJUSTABLE: Allows you to adjust the sensitivity
    values with the potentiometer as you play, but only 
    the P1 side of the board will be enabled (you can 
    configurate it as 1P or 2P in Params.h file).

**********************************************************/

#define MODE_TWO_PLAYERS

#ifdef MODE_TWO_PLAYERS
#include "two_players.h"
#elif defined(MODE_ADJUSTABLE)
#include "adjustable.h"
#endif
