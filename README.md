# Sanro - Arduino

Hardware program of the game Taiko Sanro.

## What is This Program

Music game fans from East Asia countries are most probable to know a famous game called Taiko No Tatsujin ([太鼓の達人](http://taiko-ch.net/)), developed by [Bandai Namco Entertainment](http://bandainamcoent.co.jp/).

This open-source program aims to help you develop your own **hardware taiko** at home, just like how you play taiko in arcade halls.

*This program is for personal and non-commercial use only. You may design your own taiko and have fun, but you may NOT distribute your product to the public.*

## Features

* **Full support for the PC game Taiko-san Jiro (太鼓さん次郎).** Actually, any app/game/emulator using keyboards as input is supported.
* **Force-sensitive.** I am also developing a new open-source game called Taiko Sanro that can support this feature.
* **Supports dense inputs such as rolling.**

*In one word, your taiko will perform exactly the same as the arcade version if you configure the program well enough! :D*

## Prerequisites

Because this is a DIY project, you should have some basic  electronic engineering knowledge about connecting microprocessors with jumper wires on a breadboard. **Soldering techniques, however, are NOT required.**

## Getting Started

It might take you a few days to assembly and configure your own taiko device. The chips are cheap, though, you can buy them from wherever you want.

### Preparation

Almost all of these things have alternatives, now I will show you what I used:

* [Arduino Micro](http://i.imgur.com/lXqnpJ9.jpg) module x 1
* [KEYES K-036](http://i.imgur.com/gUWnUCc.png) microphone module x 4
* Breadboard x 1
* A few jumper wires
* Micro USB cable x 1
* [Arduino IDE](https://www.arduino.cc/en/Main/Software)

And miscellaneous stuffs like:

* Thick wood plank x 4, best to be [shaped like this](http://i.imgur.com/va20eVn.jpg)
* Superglue
* Other electronical tools like screw drivers and multimeters, etc.

A few things to note:

1. Any Arduino modules with ATmega32u4 chips or Due and Zero boards are supported. Arduino Micro is the cheapest one, though.
2. Using a breadboard is a low-cost option, but it is not the best/stablest choice. I made a PCB blueprint that allows you to print the integrated board and solder up the parts.
3. You can also design and build your own microphones modules, just make sure you know how to connect them to your Arduino module.
4. Thick, solid, dense and heavy wood is the best choice, while plywood, particleboard and medium-density fiberboard (MDF) are fragile at their edges and can be easily damaged. For better experience, you should cut the planks with the shapes shown in [the picture](http://i.imgur.com/va20eVn.jpg). If you want it easier, just prepare 4 planks.

### Connecting the Parts

The scheme is quite simple. You don't even need and extra resistors or capacitors. **All you need are jumper wires.**

Each microphone module has 4 pins, and we only need 3 of them (`A0`, `+`, and `G`). Simply connect their `A0` outputs to Arduino Micro's `A0`~`A3` inputs, then connect their `+` pins together with module's `5V` pin, then the `G` pins together to the ground. Use the following picture if you have any problems.

(Picture to be uploaded)

### Uploading the Program to the Board

1. Download and install [Arduino IDE](https://www.arduino.cc/en/Main/Software).
2. Create a folder and put the source files (`sanro.ino` and `cache.h`) into it.
3. Connect your Arduino Micro to your computer with a USB cable. The driver installation should be automatic, but if you have any questions about it, [check this official guide](https://www.arduino.cc/en/Guide/ArduinoLeonardoMicro#toc8).
4. Open the `sanro` project in Arduino IDE.
5. Select "Board" - "Arduino/Genuino Micro" from the menu.
6. Compile and upload the program.

## Configuration

***WARNING: Because of the deviations between the microphones and the installation of them on the planks, you will spend much time adjusting the hardware circuits and the parameters in the program. Be patient, there are lots of tries-and-errors up ahead.***

### Hardware

Literally there is only one thing you need to do: glue each microphone to the wood plank. However, the problem is how you do it. There are two main criteria when doing this job:

* Attach the microphone to the plank as close/tight as possible; and
* Seal the microphone to isolate it from outside noises

To accomplish both, you can remove the filter covering the microphone receiver, then attach the receiver face (the microphones are usually cylindrical, like the [KEYES K-036](http://i.imgur.com/gUWnUCc.png) that I used) tightly to the surface of the plank, and seal it with superglue. In this way, the soundwave from the plank can be directly transmitted to the microphone, loud and clear. Also, noises and soundwave from nearby planks can be reduced to the minimum.

Also, please note that there is a potentiometer on the KEYES module, which is used to set the quiescent operating point of the microphone. Although I have implemented algorithms to eliminate the bias caused by unequal quiescent operating point of each microphone, **it is better to adjust the potentiometer manually and keep the quiescent operating points of the microphones at approximately the same level.** To do this, you may need to contact your microphone provider.

### Parameters in the Program

All you need to change is the `LIGHT_THRES` and the `HEAVY_THRES`, according to your microphone configuration.

The codes are short and self-explanatory, if you need help understanding them, please refer to the "About the Algorithm" part.

(To be completed)

## About the Algorithm

The algorithm in the program is simple, and there are still much more to be optimized. All pull requests are welcomed!

In short, the signal processing job can be divided into 4 steps after acquiring the samples from the analog inputs:

1. Take the derivative
2. Calculate the power of the wavaform
3. Calculate the convolution of the power
4. Find the peak of the power convolution, compare it with the thresholds to see if there is a hit

This picture shows the algorithm in a clearer way:

(Picture to be uploaded)

Step 1 is to elinimate the difference of quiescent operating point, which makes it easier to calculate the power of the waveform.

Step 2 is to enhance the signal to noise ratio (SNR), which can further eliminate the noise. `LIGHT_THRES` is also used here to cut the low-power part out.

Step 3 is to "polish" the power curve to make it more like a sequence of hit pulses, which makes it easier to find the power peak.
