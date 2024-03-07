# ZeDMD

## About

ZeDMD is a "real" DMD for pinball emulations and other use cases.

It is or will be supported by:
* [DMDExtensions](https://github.com/freezy/dmd-extensions)
* [VPX Standalone](https://github.com/vpinball/vpinball/tree/standalone)
* [PPUC](https://github.com/PPUC/ppuc)
* [batocera](https://batocera.org/)
* ...

A full tutorial of its installation is available in [English](https://www.pincabpassion.net/t14796-tuto-zedmd-installation-english) and in [French](https://www.pincabpassion.net/t14798-tuto-installation-du-zedmd)

There're four different "flawors" of the ZeDMD firmware. Because it pushes the cheap ESP32 to its limits, we can't provide a unified firmware, so you have to pick the appropriate one:
* ZeDMD: using two 64x32 panels connected over USB
* ZeDMD HD: using four 64x64 or two 128x64 panels connected over USB
* ZeDMD WiFi: using two 64x32 panels connected over WiFi (after configured over USB)
* ZeDMD HD WiFi: using four 64x64 or two 128x64 panels WiFi (after configured over USB)

Here's a short demo of ZeDMD and ZeDMD HD in parallel:

[![Watch the video](https://img.youtube.com/vi/B6D00oB4Co8/default.jpg)](https://youtu.be/B6D00oB4Co8)

## Flashing the firmware

There're different ways to flash the firmware on the ESP32.

### esptool

Download the appropriate zip file for the release section (assets) and extract it.
Install [esptool](https://github.com/espressif/esptool) and run

```shell
esptool --port /dev/ttyUSB0 --chip esp32 write_flash 0x0 ZeDMD.bin
```

For sure you have to replace the `--port /dev/ttyUSB0` optition with the serial port the ESP32 is connected to.
For Windows that might be `--port COM3` instead of `--port /dev/ttyUSB0`.

### platformio ("from source")

```shell
pio run -t uploadfs -e 128x32
pio run -t upload -e 128x32
```

### ZeDMD Updater (Windows only)

Download and install the [ZeDMD_Updater](https://github.com/zesinger/ZeDMD_Updater) and follow its instructions.

## First start

After flashing the firmware you'll see the ZeDMD logo. But due to the different panels available on the market,
you need to adjust the colors. While the logo is visible you can press the RGB button to rotate the colors until
`red` in the left top corner is red, `green` is green and `blue` is shown in blue.

Using the brightness butten you can adjust the brightness.

## IMPORTANT LEGAL NOTICE:

ZeDMD's firmware is open source and licensed as **GPLv2 or later** and can be ditributed under these terms.

For manufacturers or resellers of any shield, frame or whatever linked to the ZeDMD, our only request is that the device is
**called as "ZeDMD something"**. "ZeDMD" should be what you see first when you look at the device. Also, **a link to this project** should be provided with the device.

ZeDMD uses
* [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)
* [Bounce2](https://github.com/thomasfredericks/Bounce2)
* [miniz](https://github.com/richgel999/miniz)
* [Tiny 4x6 Pixel Font](https://hackaday.io/project/6309-vga-graphics-over-spi-and-serial-vgatonic/log/20759-a-tiny-4x6-pixel-font-that-will-fit-on-almost-any-microcontroller-license-mit)
