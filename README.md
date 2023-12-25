[日本語のドキュメントはこちら](README_ja-JP.md)

[中文文档在这里](README_zh-CN.md)

![Taiko Drum Controller](./images/banner-taiko.png)

# Taiko Drum Controller - Arduino (ATmega32U4/ESP32)

Open-source hardware program to make your own Taiko no Tatsujin PC controller.

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
