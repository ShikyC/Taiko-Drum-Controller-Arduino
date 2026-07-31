> **Legacy implementation archived:** the old Arduino/ATmega32U4 and earlier ESP32 implementation from `master` has reached end of life and is archived on the [`archive-arduino-legacy` branch](https://github.com/ShikyC/Taiko-Drum-Controller-Arduino/tree/archive-arduino-legacy). This branch documents the refactored ESP-IDF implementation.

![Taiko Drum Controller](./images/shell.png)

# Taiko Drum Controller - ESP32-S3

Open-source firmware and hardware for building a USB taiko drum controller for PC play.

This version is intended for Taiko Force Lv. 6 style drums and other two-player, eight-sensor (1P x 4 + 2P x 4) taiko builds. For Taiko Force Lv. 5 and earlier versions, check the [archived branch](https://github.com/ShikyC/Taiko-Drum-Controller-Arduino/tree/archive-arduino-legacy).

## Current Status

- [x] Supports Taiko Force Lv. 6 drum wiring through ESP32-S3 ADC continuous mode with DMA.
- [x] Processes two players; the current detector is calibrated against 1P data.
- [x] Sends analog hit strength through gamepad axes instead of keyboard events.
- [x] PCB Gerber files and BOM are available in [`PCB/`](./PCB/).
- [x] [3D printed shell](./PCB/3D_Print_Shell.3mf) is ready.
- [ ] Misc firmware improvements under construction.

## Hardware Support

The supported target is **ESP32-S3**.

Other ESP32 variants may work if they support the same ADC continuous mode, DMA behavior, USB device mode, and pin availability, but they are not tested. Arduino boards, ATmega32U4 boards, and non-USB ESP32 development boards are not supported by this refactored firmware.

## Firmware Overview

The firmware in [`main/taiko_controller.c`](./main/taiko_controller.c) does six main things:

1. Configures TinyUSB as a HID gamepad.
2. Configures ADC continuous sampling for eight drum sensor inputs.
3. Reassembles complete eight-channel scans independent of DMA frame boundaries.
4. Converts raw ADC readings through an eFuse-calibrated millivolt lookup table.
5. Runs baseline removal, a 0.96 ms RMS window, winner selection, and hit/rearm state through the platform-independent processor in [`main/taiko_hit_processor.c`](./main/taiko_hit_processor.c).
6. Publishes the winning zone and strength through signed gamepad axes.

The current sampling model is:

- `PLAYERS`: `2`
- `CHANNELS_PER_PLAYER`: `4`
- `TOTAL_CHANNELS`: `8`
- requested aggregate ADC rate: `83333` conversions/s
- actual aggregate ADC rate: `83333.333` conversions/s
- actual per-channel rate: `10416.667` samples/s
- conversion spacing: `12 us`
- `USB_REPORT_INTERVAL_US`: `1000`
- detector integration and capture latency: about `1.73 ms`
- output hold and refractory interval: `12 ms`

Each player has four zones:

1. Left don
2. Left kat
3. Right don
4. Right kat

Only the strongest zone for each player is emitted in each HID report. Don zones are sent as positive axis values and kat zones are sent as negative axis values.

## Pin Map

The default ADC pin map uses ADC1 GPIOs on ESP32-S3 and avoids the native USB pins.

| Player | Zone | GPIO |
| --- | --- | --- |
| P1 | Left don | 3 |
| P1 | Left kat | 4 |
| P1 | Right don | 5 |
| P1 | Right kat | 6 |
| P2 | Left don | 7 |
| P2 | Left kat | 8 |
| P2 | Right don | 9 |
| P2 | Right kat | 10 |

Debug outputs:

| GPIO | Meaning |
| --- | --- |
| 1 | High when the ADC DMA pool overflows or an ADC read fails |
| 2 | High when the USB HID host is not ready |

If you change pins, use ADC-capable pins for the selected ESP32-S3 board and keep GPIO 19/20 free for native USB unless your board routes USB differently.

## Requirements

- ESP32-S3 development board with native USB device support.
- ESP-IDF 5.x environment.
- Four piezo sensors per drum, eight total for two-player support.

Hardware files:

- Project PCB, Gerber archive, and BOM are available in [`PCB/`](./PCB/).
- (Optional) 3D printed shell.

## PCB

The current PCB package is available under [`PCB/`](./PCB/):

- [`PCB/PCB.png`](./PCB/PCB.png): PCB preview image.
- [`PCB/Taiko_DMA_PCB.zip`](./PCB/Taiko_DMA_PCB.zip): Gerber production archive.
- [`PCB/Taiko_DMA_BOM.xlsx`](./PCB/Taiko_DMA_BOM.xlsx): BOM spreadsheet.

![Taiko DMA PCB](./PCB/PCB.png)

The board is designed around the ESP32-S3-WROOM-1U module and the DMA-based firmware in this repository. Review the BOM and board files before ordering or assembly, especially while the housing and final assembly guide are still in progress.

## 3D Printing

The [shell's 3MF file](./PCB/3D_Print_Shell.3mf) is ready to be used with any slicer software and 3D printers. Tested with PLA. Note that for the best result, use 0.2mm nozzle. Larger nozzle can cause some thin walls to be broken.

To finish the build, in addition to the PCB BOM, you'll need:

- M3 countersunk screw x 4
- M3 heat inserts x 4

An assembled example is shown below.

![Example](./images/components.png)

## Build and Flash

Install and activate ESP-IDF, then build for ESP32-S3:

```sh
idf.py set-target esp32s3
idf.py build
```

If you see build errors, make sure to set the following [`sdkconfig.defaults`](./sdkconfig.defaults) value:

```ini
CONFIG_TINYUSB_HID_COUNT=1
```

To enter flashing mode, press and hold the flash button, then plug in the USB cable, and flash the firmware:

```sh
idf.py flash
```

After flashing, unplug and replug in the USB cable (without holding the flash button). You will see an HID gamepad called "Taiko Controller" connected.

## Development

For online debugging, use [this tool](https://shiky.me/taiko). For deeper debugging, you'll need an ESP32-S3 development board, as the production PCB lacks the serial port. 

![Online tool](./images/online_tool.png)

## Gamepad Output

The USB device descriptor is configured as:

- Manufacturer: `Taiko Community`
- Product: `Taiko Controller`
- VID: `0x4869`
- PID: `0x4869`

Current axis mapping:

| Player | Zone | HID output |
| --- | --- | --- |
| P1 | Left don | `+X` |
| P1 | Left kat | `-X` |
| P1 | Right don | `+Y` |
| P1 | Right kat | `-Y` |
| P2 | Left don | `+Rx` |
| P2 | Left kat | `-Rx` |
| P2 | Right don | `+Ry` |
| P2 | Right kat | `-Ry` |

## Tuning

Choose the compile-time preset in [`main/taiko_controller.c`](./main/taiko_controller.c):

```c
#define HIT_SENSITIVITY TAIKO_SENSITIVITY_BALANCED
```

Available presets are `TAIKO_SENSITIVITY_SENSITIVE`, `TAIKO_SENSITIVITY_BALANCED`, and `TAIKO_SENSITIVITY_FIRM`. They differ in incidental-contact rejection and axis scaling; Balanced is the default. The thresholds and per-channel gains live in [`main/taiko_hit_processor.c`](./main/taiko_hit_processor.c).

Only the 1P detector is currently calibrated. The existing P2 pins run the same processor, but the different P2 drum waveform needs its own capture and calibration before it should be treated as validated.

## Signal Conditioning Notes

Piezo sensors can produce voltage outside the safe ADC input range. Protect the ESP32-S3 ADC inputs before connecting a drum directly.

The legacy implementation documented bridge rectifiers because raw piezo output can swing negative and lose useful signal when clipped by the ADC. That concern still applies at the hardware level, but the new firmware is centered on high-rate ADC sampling rather than the old Arduino threshold detector.

Even with the PCB package available, treat the wiring and analog front end as experimental until the housing and final assembly guide are finished.

## Credits

- The PCB design is based on the open-source ESP32-S3 minimal system board project published on OSHWHub: [esp32s3-zui-xiao-xi-tong-ban-20241211](https://oshwhub.com/sun1053/esp32s3-zui-xiao-xi-tong-ban-20241211).
- The ADC DMA implementation is based on Espressif's official ESP-IDF ADC continuous mode example code.

## Appendix: Game Integration Notes

You will need a compatible [Taiko Arcade Loader](https://github.com/esuo1198/TaikoArcadeLoader) library to make the controller work with the game instance. After importing the required files, open the `gamecontrollerdb.txt` file and add this following line:

```
030052a8694800006948000000000000,Taiko Controller,-leftx:-a0,+leftx:+a0,-lefty:-a1,+lefty:+a1,-rightx:-a2,+rightx:+a2,-righty:-a3,+righty:+a3,platform:Windows,
```

Then, open `config.toml`, find the `[controller]` section, set `analog_input = true`. This will disable button input for the game and use analog axes' inputs. Now you can start the game and try out the new controller!
