[日本語のドキュメントはこちら](README_ja-JP.md)

[中文文档在这里](README_zh-CN.md)

![Taiko Drum Controller](./images/banner-taiko.png)

# Taiko Drum Controller - Arduino (ATmega32U4/ESP32)

Open-source hardware program to make your own Taiko no Tatsujin PC controller. It can emulate as a keyboard, or as an analog joystick to enable hitting force sensing - just like how you play on the arcade machine. Now also support 2 drums so you can enjoy the game together with your friends at home!

## About this Project

This project aims to help you develop your own hardware taiko at home.

**This program is for personal and non-commercial use only.**

## What You Need

1. An Arduino Micro/Leonardo (ATmega32U4) board or an Arduino Nano ESP (ESP32) board.
   
   Most ATmega32U4 boards work, but you need to verify that they support keyboard emulation; ATmega328P boards like Arduino Uno don't work.
   
   ESP32 is strongly recommended because it's significantly more powerful than ATmega32U4. This project uses an ESP32-WROOM-32 board.

2. 4 piezoelectric sensors.
   
3. 8 100kΩ resistors.
   
4. (Optional) 4 bridge rectifier chips such as [DB107](https://www.diodes.com/assets/Datasheets/products_inactive_data/ds21211_R5.pdf) (see the Additional Notes section for details).

5. (Optional) Some red and blue LEDs.
   
6. Necessary electronic components (breadboards and jumper wires, etc.).
   
7. Wood planks and cutting tools (only if you need to make your physical taiko drum from scratch). If you have an aftermarket taiko or a Big Power Lv. 5 drum, you can use them directly.

## Steps to Make the Controller

1. Make the drum and firmly glue the 4 piezoelectric sensors to the drum. Refer to the image for preferred locations of the sensors.
   
   ![Sensor Setup](./images/piezo_locations.png)

2. Connect the piezoelectric sensors and other components to the controller as follows (the polarity of the piezoelectric sensors don't matter);

   The following schemes are for Arduino Micro boards. If you use a different board, refer to its documentations for the connection.
   
   ![Controller scheme](./images/scheme.png)

   If you choose to add the bridge rectifiers, use the following scheme:
   
   ![Controller scheme with bridge rectifiers](./images/scheme_bridge.png)

3. Flash the firmware to the board.
   
   You may need to fine-tune some parameters like `SAMPLE_CACHE_LENGTH`, `HIT_THRES`, `RESET_THRES`, and `sensitivity`. See the following section for details. 

4. Have fun!

## Tuning the Parameters

1. Hit and reset threshold
   
   Set `DEBUG 1` (this disables the keyboard output and sends signal values from the serial port), flash the firmware, roll on one of the 4 areas of the drum, and visualize the graph from the output of the serial monitor. The hit threshold should be lower than your heaviest hit on the drum, and the reset threshold should be greater than the trough between roll hits. The reset value should also be below the hit value.
   
   Repeat the process for the rest 3 areas and find the best one that fits all.

   ![Setting hit and reset values](./images/tune_hit_reset.png)

2. Sampling length
   
   For maximum runtime speed, the `cache.h` library has been optimized to work with `SAMPLE_CACHE_LENGTH` window sizes of powers of 2. That means 2, 8, 16, and 32, etc. Practically 16 is the best value for Arduino, but if you have a powerful microcontroller that samples the input at the speed of at least 4000Hz or more, you can change the value to 32 for a smoother (in other words, less noisy) curve.

3. Sensitivities
   
   Not all piezoelectric sensors are the same, and due to installation errors, the captured signals from the 4 sensors may vary significantly. The sensitivity values are multipliers to normalize the differences. In the following example, the right-don area generates a much higher value than the rest 3, so you can adjust `sensitivity` to `{1.0, 1.0, 0.5, 1.0}` to eliminate the issue.

   ![Setting sensitivity values](./images/tune_sensitivities.png)

   Note that the installation of the sensors is very critical. You should make sure that the sensors are firmly attached on the wood and located properly.

## Additional Notes

1. Why using bridge rectifiers

   Without biasing the voltage of the piezoelectric sensors, their output voltage range is about -5V to +5V. However, the ADCs of the analog inputs only accepts positive voltage values (0-3.3V for ESP32 and 0-5V for ATmega32U4). When they receive a negative voltage, it's simply truncated to 0.
   
   It's usually okay for normal electronic drums because we're just losing half of the input energy and it doesn't influence how we calculate the hitting time. But it can cause problems for *taiko* drums, especially with slow processors like ATmega32U4. 
   
   In a taiko drum, all the 4 vibrating pieces are connected together, meaning that if you hit left-don, the processor also receives signals from left-kat, right-don, and right-kat. If the left-don piezoelectric sensor generates a negative voltage at the beginning and is truncated by the ADC, it will cause a minor "delay" of about 3 to 4 milliseconds, and the processor could incorrectly treat this hit as a right-don, a left-kat, or even a right-kat, whichever sends a highest positive value.

   Using a bridge rectifier, all negative values are converted to positive. In other words, it's like the `abs()` function, ensuring that we don't lose any negative voltages.

   ![Why using bridge rectifiers](./images/bridge_signal.png)

# Taiko Controller - Analog Input Mode (Beta)

With ESP32-S2 or ESP32-S3 controllers, instead of keyboard emulation, the drum controller can work as a gamepad and send its axes values to the game (which also must support analog input). In this way, the game can recognize different force levels of the hit.

If you prefer to use the Arduino Micro/Leonardo board, please refer to the [Arduino XInput Library](https://github.com/dmadison/ArduinoXInput) to implement the gamepad.

## What You Need

1. Make your drum or use Taiko Force Lv.5.

2. Flash `ESP32-S3-Analog/ESP32-S3-Analog.ino` to your controller.

3. A working ***game***, with these modifications:

   - Backup and replace the `bnusio.dll` file in the game folder with the one here in the `extra/` folder.

     This file is compiled from [this fork](https://github.com/ShikyC/TaikoArcadeLoader/tree/Refactor) and you can compile it by yourself if you want.

     *This modified library only works with a specific game version. If it breaks your game, please clone the original repository, make the corrensponding modifications, and compile it.*

   - Open the `gamecontrollerdb.txt` file in the game folder and add one entry under `#Windows`: 
   
     `030052a8694800006948000000000000,Taiko Controller,-leftx:-a0,+leftx:+a0,-lefty:-a1,+lefty:+a1,-rightx:-a2,+rightx:+a2,-righty:-a3,+righty:+a3,platform:Windows,`

     This will tell the game that our ESP32 controller is a gamepad called "Taiko Controller", and map the axis to the standard SDL2 library so that the game can recognize the analog inputs.

   -  Open the `config.toml` file and add the following lines at the end:
     
     ```
     [controller]
     analog = true
     ```

     Note that with `analog = true`, all the keyboard drum inputs are disabled. Sorry for this but it need further refactoring to make them work together. If you want to switch back to keyboard inputs, set `analog = false`.

4. Launch the game and enjoy!