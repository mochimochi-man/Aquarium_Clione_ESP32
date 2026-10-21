# Aquarium Clione ESP32

![Aquarium Clione ESP32](screenshot.jpg)

A clione (sea angel) tank for an ESP32-C3 or ESP32-S3 with an ST7789 (320x240, landscape).
Each time it starts, 1 to 5 clione appear at random positions, and once every 12 to 24 hours a small sea snail drifts in and one of them feeds on it.
The target is detected automatically with `CONFIG_IDF_TARGET_ESP32C3` in `lgfx_setup.h` and the sources, which switches the wiring and the drawing path.

## Wiring (ESP32-C3 Dev Module)

|ST7789|GPIO|
|-|-|
|SCLK / SCL|8|
|MOSI / SDA|10|
|DC|4|
|CS|5|
|RST / RES|3|
|VCC|3V3|
|GND|GND|

## Wiring (ESP32-S3 Dev Module)

|ST7789|GPIO|
|-|-|
|SCLK / SCL|12|
|MOSI / SDA|11|
|DC|9|
|CS|10|
|RST / RES|tied to 3V3 (define `PIN_TFT_RST` to use a free GPIO)|
|VCC|3V3|
|GND|GND|

On either target you can change the wiring by defining `PIN_TFT_SCLK` / `PIN_TFT_MOSI` / `PIN_TFT_DC` / `PIN_TFT_CS` / `PIN_TFT_RST` / `PIN_TFT_BLK` / `TFT_SPI_FREQ` before building.

## Build / flash

Open `Aquarium_Clione_ESP32.ino` in the Arduino IDE and use the settings below.

### ESP32C3 Dev Module

- Board: **ESP32C3 Dev Module**
- USB CDC On Boot: **Enabled**
- Flash: **4MB**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
- Library: **LovyanGFX**

### ESP32-S3 Dev Module

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- USB Mode: **Hardware CDC and JTAG**
- Flash: **16MB**
- Partition Scheme: **16MB Flash(3MB APP/9.9MB FATFS)**
- PSRAM: **Enabled**
- Library: **LovyanGFX**

Select the port of the board connected over USB and upload.

## Serial commands (115200bps)

|Command|Action|
|-|-|
|FEED|Drop in food|
|AUTO ON / AUTO OFF|Turn automatic feeding on/off|
|SHOW|Show status and drawing speed|
|RESET|Reset the tank (the number of clione is drawn again)|
|REBOOT|Restart|
|HELP|List commands|

## License

Copyright of this project belongs to mochimochi-man (X : @MochimochimanEX).
It is MIT licensed, so please feel free to use it.
The clione images were generated with AI.
