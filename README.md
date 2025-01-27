# ZeDMD

## About

ZeDMD is a "real" DMD designed for pinball emulations and other use cases. Originally developed by David "Zedrummer" Lafarge, the concept laid the foundation for what ZeDMD has become today. Markus Kalkbrenner, the current maintainer of ZeDMD, was inspired by the original idea and took the initiative to further develop and enhance it into the robust and versatile solution it is now.

ZeDMD is or will be supported by:
* [DMDExtensions](https://github.com/freezy/dmd-extensions)
* [VPX Standalone](https://github.com/vpinball/vpinball/tree/standalone)
* [PPUC](https://github.com/PPUC/ppuc)
* [batocera](https://batocera.org/)
* [libdmdutil](https://github.com/vpinball/libdmdutil)

A full tutorial of its installation is available in [English](https://www.pincabpassion.net/t14796-tuto-zedmd-installation-english) and in [French](https://www.pincabpassion.net/t14798-tuto-installation-du-zedmd)

Meanwhile, there are different "flavours" of the ZeDMD firmware. Because it pushes the cheap ESP32 to its limits, we can not provide a unified firmware, so you have to pick the appropriate one:
* ZeDMD 128x32: using two 64x32 panels driven by an ESP32 connected over USB or WiFi
* ZeDMD HD 256x64: using four 64x64 or two 128x64 panels driven by an ESP32 connected over USB or WiFi
* ZeDMD 128x64: using one 128x64 panel driven by an ESP32 connected over USB or WiFi, showing 128x32 content with an offset, suitable for mini cabinets
* ZeDMD S3 128x32: using two 64x32 panels driven by an ESP32 S3 N16R8 connected over USB CDC or WiFi
* ZeDMD S3 HD 256x64: using four 64x64 or two 128x64 panels  driven by an ESP32 S3 N16R8 connected over USB CDC or WiFi
* ZeDMD S3 128x64: using one 128x64 panel driven by an ESP32 S3 N16R8 connected over USB CDC or WiFi, showing 128x32 content with an offset, suitable for mini cabinets
* ZeDMD S3 AMOLED: using a small OLED driven by a LilyGo AMOLED T-Display-S3 V2 connected via USB CDC
* ZeDMD S3 AMOLED WiFi: using a small OLED driven by a LilyGo AMOLED T-Display-S3 V2 connected via WiFi

Here is a short demo of ZeDMD and ZeDMD HD in parallel:

[![Watch the video](https://img.youtube.com/vi/B6D00oB4Co8/default.jpg)](https://youtu.be/B6D00oB4Co8)

## Flashing the firmware

There are different ways to flash the firmware on the ESP32.

### esptool

Download the appropriate zip file from the [latest release](https://github.com/PPUC/ZeDMD/releases/latest)'s assets section and extract it.

Install [esptool](https://github.com/espressif/esptool)

On Windows you should use `esptool.exe` instead of `esptool`.
If you have different devices attached via USB or if the ESP32 is not detected you could specifiy the concrete port.
For a Windows machine this could be:
```shell
esptool.exe --chip esp32 --port COM3 write_flash 0x0 ZeDMD.bin
```
On a unix-like system:
```shell
esptool --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 ZeDMD.bin
```

The ESP32-S3 N16R8 is now fully supported too. To flash this device, simply modify the command seen above by appending `s3` to `esp32`, resulting in `esp32s3`.

### platformio ("from source")

```shell
pio run -t uploadfs -e 128x32
pio run -t upload -e 128x32
```

### ZeDMD Updater (Windows only)

Download and install the [ZeDMD_Updater](https://github.com/zesinger/ZeDMD_Updater) and follow its instructions.

## ZeDMD pinout diagram
ZeDMD utilizes HUB75 to display full-color content on your panels. To achieve this, the panels must be connected to specific GPIOs on your ESP32.
| ESP32 Dev Board | ESP32-S3-N16R8 | HUB75 pins |      
| -------------   | -------------  | ---------- |         
| GPIO 25         | GPIO 4         | R1         |          
| GPIO 27         | GPIO 6         | B1         |
| GPIO 14         | GPIO 7         | R2         |
| GPIO 13         | GPIO 16        | B2         |
| GPIO 23         | GPIO 18        | A          |
| GPIO 5          | GPIO 3         | C          |
| GPIO 16         | GPIO 41        | CLK        |
| GPIO 15         | GPIO 2         | OE         |
| GPIO 26         | GPIO 5         | G1         |
| GPIO 12         | GPIO 15        | G2         |
| GPIO 22         | GPIO 1         | E          |
| GPIO 19         | GPIO 8         | B          |
| GPIO 17         | GPIO 42        | D          |
| GPIO 4          | GPIO 40        | LAT        |

To navigate the menu and adjust settings, you'll need to configure a few buttons. However, only two buttons are essential to modify values and exit the menu. These two buttons are `Menu Left` and `Value +`.
| ESP32 Dev Board | ESP32-S3-N16R8 | Menu Button |      
| -------------   | -------------  | ------------|         
| GPIO 0          | GPIO 48        | Menu Left   |          
| NOT USED        | GPIO 47        | Menu Right  |
| GPIO 18         | GPIO 0         | Value +     |
| NOT USED        | GPIO 45        | Value -     |

## First start

After flashing the firmware the ZeDMD logo will appear. Due to the variety of panels available on the market, you’ll need to adjust the RGB values. On ZeDMD versions prior to v5.0.0, this can be done by pressing the RGB button. From v5.0.0 onwards, you can adjust the RGB values by navigating to the `RGB Order:` option at the top of the settings menu.
Then, adjust the RGB order by rotating the colors until the following alignment is achieved:
* The top-left corner displays `red` as red.
* `Green` appears as green.
* `Blue` is shown as blue.
  
Versions prior to V5.0.0 let you adjust the brightness using the brightness button. 
From v5.0.0 onwards, this is done by navigating to the `Brightness:` option in the settings menu.
> [!IMPORTANT]
> From version 5.0.0 onwards: once you’ve finished changing values, you must navigate to the 'Exit' button. This step is required to enable the ZeDMD to enter handshake mode.

## ZeDMD-WiFi

After activating the WiFi mode in the settings menu, connect your mobile device or laptop to the `ZeDMD-WiFi` network using the password `zedmd1234`.
Then open your web browser and navigate to http://ZeDMD-WiFi.local (IP: 192.168.4.1) to access the configuration settings.

## IMPORTANT LEGAL NOTICES:

ZeDMD's firmware is open source and licensed as **GPLv2 or later** and can be ditributed under these terms.

For manufacturers or resellers of any shield, frame, ready-to-use devices or whatever linked to the ZeDMD, our only request is that the device is **called as "ZeDMD something"** or **powered by ZeDMD**. "ZeDMD" should be what you see first when you look at the device. Also, **a link to this project** must be provided with the device.

ZeDMD uses
* [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)
* [Bounce2](https://github.com/thomasfredericks/Bounce2)
* [miniz](https://github.com/richgel999/miniz)
* [Tiny 4x6 Pixel Font](https://hackaday.io/project/6309-vga-graphics-over-spi-and-serial-vgatonic/log/20759-a-tiny-4x6-pixel-font-that-will-fit-on-almost-any-microcontroller-license-mit)
* [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
* [JPEGDEC](https://github.com/bitbank2/JPEGDEC)
* [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
* [RM67162 with fixes from Nikthefix](https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED/issues/2)

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

### ZeDMD S3 crashes when connected via USB to a Windows machine

This is a known issue. ZeDMD S3 works perfectly well with Linux and macOS. But if you're using Windows you should use the WiFi mode.

### I have installed all of the latest files, but I still get crashes on a Windows machine
A few users have reported that VPX and ZeDMD consistently crash if the latest Visual C++ Redistributable Runtime packages are not installed. To resolve this issue, ensure you have the most up-to-date runtime packages installed. If the latest version doesn’t resolve the issue, it may be necessary to install all available versions of the Visual C++ Redistributable Runtime packages.

### ZeDMD S3 crashed, how can I help fixing the issue

If you discover a crash, there's a good chance that a coredump has been written to flash. If you flashed the device using `pio`, you can extract and interpret the coredump:
```shell
python ~/esp/v5.3.2/esp-idf/components/espcoredump/espcoredump.py info_corefile .pio/build/S3-N16R8_128x32/firmware.elf
```


