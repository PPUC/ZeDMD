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

## IMPORTANT LEGAL NOTICES:

ZeDMD's firmware is open source and licensed as **GPLv2 or later** and can be ditributed under these terms.

For manufacturers or resellers of any shield, frame, ready-to-use devices or whatever linked to the ZeDMD, our only request is that the device is **called as "ZeDMD something"** or **powered by ZeDMD**. "ZeDMD" should be what you see first when you look at the device. Also, **a link to this project** must be provided with the device.

ZeDMD uses
* [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)
* [Bounce2](https://github.com/thomasfredericks/Bounce2)
* [miniz](https://github.com/richgel999/miniz)
* [Tiny 4x6 Pixel Font](https://hackaday.io/project/6309-vga-graphics-over-spi-and-serial-vgatonic/log/20759-a-tiny-4x6-pixel-font-that-will-fit-on-almost-any-microcontroller-license-mit)

## FAQ

### "Where can I buy a ZeDMD?"

The intention of ZeDMD is to provide a cheap DIY DMD solution. The maintainers of this project don't run any shop to sell ready-to-use hardware!

Nevertheless, there're are some shops we're aware of who designed their own shields to build a ZeDMD.
And as this might ease the task to use a ZeDMD for some users, we're fine to add some links here:
* https://shop.arnoz.com/en/dmd/87-esp-dmd-shield.html
* https://benfactory.fr/produit/zedmd-shield/
* https://www.smallcab.net/shield-zedmd-p-2697.html

There're also ready-to-use devices:
* https://benfactory.fr/produit/zedmd/
* https://www.smallcab.net/pack-zedmd-p-2698.html
* https://virtuapin.net/index.php?main_page=product_info&cPath=6&products_id=283

### "The LED pannels aren't working", ghosting, wrong pixels, missing lines

The ZeDMD firmware supports a wide range of LED panels with different driver chips ... in theory.
In general, some driver chips require adjustments in the configuration, timings and the clock phase.
That can't be done with ZeDMD updater, but within the source code of the firmware. Here os some background information:
* https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA?tab=readme-ov-file#supported-panel-can-types
* https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA?tab=readme-ov-file#latch-blanking

These are the available config options:
https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA/blob/54ef6071663325e7b8f3a9e1e0db89b0b0b7398d/src/ESP32-HUB75-MatrixPanel-I2S-DMA.h#L235-L309

The pre-built firmware uses the default config which is suitable for the most common LED panels.
Obviously we can't provide a menu on the device to adjust these settings as you won't see them ;-)

But we consider to add support for these driver settings to libzedmd and the ini file of dmdserver so that these values could be adjusted and sent to ZeDMD before the panels get initialized.

We could also offer firmeware builds for specific panels. But that would require that somone sends such panels to us to find out the correct config.

If you find out what config adjustment gets a specific panel to work, you should open an issue here and provide that information so that we could include it in the README and probably add a specific automated build for the bext releases.

### "I'm running a shop, can I assemble and sell ZeDMDs?"

Yes, you can, as long as you respect the legal notices above. You could also do your own hardware design. But it would be nice if you sent us a ready to-use device for testing because people might ask here or on discord about problems with a specific variation of ZeDMD.
