[日本語のドキュメントはこちら](README_ja-JP.md)

[中文文档在这里](README_zh-CN.md)

![Taiko Drum Controller](./images/banner-taiko.png)

# Taiko Drum Controller - Arduino

Open-source hardware program to make your own Taiko no Tatsujin PC controller.

## About this Project

This project aims to help you develop your own **hardware taiko** at home.

*This program is for personal and non-commercial use only.*

## What You Need

1. An Arduino Micro or Leonardo microcontroller (other compatible boards might work, but you need to verify that they support keyboard emulation);
   
2. 4 piezoelectric sensors;
   
3. Necessary electronic components (breadboards, resistors, LEDs, jumper wires, etc.);
   
4. Wood planks and cutting tools if you need to make your drum from scratch. If you have a aftermarket taiko or a Big Power Lv. 5 drum, you can use them directly.

## Steps to Make the Controller

1. Make the drum and firmly glue the 4 piezoelectric sensors to the drum. Refer to the image for preferred locations of the sensors.
   
   ![Controller scheme](./images/piezo_locations.png)

2. Connect the piezoelectric sensors and other components to the controller as follows (the polarity of the piezoelectric sensors don't matter);
   
   ![Controller scheme](./images/scheme.png)

3. Flash the firmware to the board.
   
   You may need to fine-tune some parameters like `SAMPLE_CACHE_LENGTH`, `HIT_THRES`, `RESET_THRES`, and `sensitivity`. See the following section for details. 

4. Have fun!

## Tuning the Parameters

1. Hit and reset threshold
   
   Set `DEBUG 1` (this disables the keyboard output and sends signal values from the serial port), flash the firmware, roll on one of the 4 areas of the drum, and visualize the graph from the output of the serial monitor. The hit threshold should be smaller than your heaviest hit on the drum, and the reset threshold should be greater than the trough between roll hits. The reset value should also below the hit value.
   
   Repeat the process for the rest 3 areas and find the best one that fits all.

   ![Controller scheme](./images/tune_hit_reset.png)

2. Sampling length
   
   For maximum runtime speed, the `cache.h` library has been optimized to work with `SAMPLE_CACHE_LENGTH` window sizes of powers of 2. That means 2, 8, 16, and 32, etc. Practically 16 is the best value for Arduino, but if you have a powerful microcontroller that samples the input at the speed of at least 4000Hz or more, you can change the value to 32 for a smoother (in other words, less noisy) curve.

3. Sensitivities
   
   Not all piezoelectric sensors are the same, and due to installation errors, the captured signals from the 4 sensors may vary significantly. The sensitivity values are multipliers to normalize the differences. In the following example, the right-don area generates a much higher value than the rest 3, so you can adjust `sensitivity` to `{1.0, 1.0, 0.5, 1.0}` to eliminate the issue.

   ![Controller scheme](./images/tune_sensitivities.png)

   Note that the installation of the sensors is very critical. You should make sure that the sensors are firmly attached on the wood and located properly.
