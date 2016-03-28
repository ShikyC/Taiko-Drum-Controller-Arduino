# Sanro - Arduino

Hardware program of the game Taiko Sanro.

## What is This Program

Music game fans from East Asia countries are most probable to know a famous game called Taiko No Tatsujin (太鼓の達人), developed by Bandai Namco Games of Japan. This program aims to help you develop your own **hardware taiko** at home, just like how you play Taiko in arcade halls.

## Features

* **Full support for the PC game Taiko-san Jiro (太鼓さん次郎).** Actually, any app using keyboards as input is supported.
* **Force-sensitive.** I am also developing a new open-source game called Taiko Sanro that can support this feature.
* **Supports dense inputs such as rolling.**

*In all, if you configure the program well enough, your taiko will perform exactly the same as the arcade version! :D*

## Prerequisites

Because this is a DIY project, you should have some basic  electronic engineering knowledge about connecting microprocessors with jumper wires on a breadboard. **Soldering techniques are NOT required.**

## Getting Started

It might take you a few days to assembly and configure your own taiko device. The chips are cheap, though, you can buy them from wherever you want.

### Preparation

* Arduino Micro board x 1
* Keyes K-036 microphone module x 4
* Breadboard x 1
* A few jumper wires
* Micro-USB to USB cable x 1
* [Arduino IDE](https://www.arduino.cc/en/Main/Software)
* Wood planks x 4, shaped like [this]()

A few things to note:

1. Any Arduino modules with ATmega32u4 chips or Due and Zero boards are supported. Arduino Micro is the cheapest one, though.
2. Using a breadboard is a low-cost option, but it is not the best/stablest choice. I made a PCB blueprint that allows you to print the integrated board and solder up.
3. You can also design build your own microphones modules, just make sure you know how to connect them to your Arduino module.
4. About the wood planks: solid, dense and heavy wood is the best choice, while plywoods, particleboard and medium-density fiberboard (MDF) is fragile at edges and can be easily damaged.

### Connecting the Parts

The schema is quite simple. Each microphone module has 4 pins, and we only need 3 of them (`A0`, `+`, and `G`). Simply connect their `A0` outputs to Arduino Micro's `A0`~`A3` inputs, then connect their `+` pins together with module's `5V` pin, then the `G` pins together to the ground. Use the following picture if you have any problems.

### Uploading the Program

1. Create a folder and put the source files (`sanro.ino` and `cache.h`) into it.
2. Download and install [Arduino IDE](https://www.arduino.cc/en/Main/Software).
3. Connect your Arduino Micro to your computer with a USB cable. The driver installation should be automatic, but if you have any questions about it, [check this official guide](https://www.arduino.cc/en/Guide/ArduinoLeonardoMicro#toc8).
4. Open the `sanro` project in Arduino IDE.
5. Select "Board" - "Arduino/Genuino Micro" from the menu.
6. Compile and upload the program.

## Configuration

