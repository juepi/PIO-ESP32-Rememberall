# PIO-ESP32-Rememberall
The Rememberall - equipped with a WS2812 LED ring and a ePaper display - will help you keeping track of easily forgotten events.  
The project includes a PowerShell based feeder script which allows you to load events from multiple iCal calendar files (or URLs, in example google calendars), find the next upcoming event and send the processed data to your MQTT broker where the Rememberall will pick it up.  
**Note:** This project requires a NTP server (either local or internet).

## Design Goals
The Rememberall has been designed to run on a WEMOS S2 Mini plugged in a [ESP-Mini-Base](https://github.com/juepi/ESP-Mini-Base). The whole setup is powered by a single LFP cell (in my case two 32700 round cells in parallel). To maximize battery lifetime, the code has been designed put the ESP in DeepSleep as much as possible and also reduce power usage by enabling WiFi only from time to time to check für updates. To further reduce power requirements, the ESP's CPU clock has been limited to 80MHz (see `platformio.ini`).
The code is based on my [PIO-ESP32-Template](https://github.com/juepi/PIO-ESP32-Template), also consult the readme in that repo for requirements and flashing guide.

## Hardware
* [WEMOS S2 Mini](https://www.wemos.cc/en/latest/s2/s2_mini.html)
* [ESP-Mini-Base](https://github.com/juepi/ESP-Mini-Base) (optional)
* [WaveShare GDEW0213Z16 2.13"](https://www.waveshare.com/product/displays/e-paper/epaper-3/2.13inch-e-paper-hat-g.htm) 3-color ePaper display
* WS2812 RGB LED-Ring with 32 LEDs (AliExpress, Amazon etc.)
* Non-latching Pushbutton and LED (both optionally)

**Note:** The ESP-Mini-Base is optional. If you do not plan to use it, you have to take care of:  
* Reading VCC (external attenuator 12k / 3k6 voltage divider, 100nF ceramic capacitor recommended on ESP ADC pin)
* ability to power off LED ring (draws quite some current when off (show black), which would also be drawn during DeepSleep)

## MQTT Topics

All MQTT topics mentioned below need to be set **retained**, as the Rememberall requires all of them to be fetched at startup.

### /Your/Topic/Tree/eventTxt
This topic contains the text to be displayed on the ePaper display. It may contain 1, 2 or 3 lines of text and needs to be formatted like this (example for 3 lines of text):  
``LineCount|ColorLine1;TextLine1|ColorLine2;TextLine2|ColorLine3;TestLine3``  
The `LineCount` must be an integer of 1-3 indicating how much text lines the message contains. `ColorLineX` is the text color (per line), where `0` equals **black** and `1` equals **red**.  
**Attention:** The characters `|` and `;` must not be used in the `Summary` field (description) of your calendar events, as they are used as seperators. If you are using the provided PoSh feeder, the first word of the `Summary` field will be used to describe the event. Note that the text will be truncated to 10 characters, which can be displayed per ePaper line.

### /Your/Topic/Tree/eventReminder
This topic contains the deadline (which equals the start date of an event), the start time of the 2 reminder periods and the color which the LED ring should show for the current event. Example:  
``65ab999f|65aa481f|65aaf0df|0xFF00FF``  
Timestamps are encoded in **Unix Epoch time** and in **hexadecimal base** to shorten the strings. The LED color is encoded as `0xRRGGBB`.  
**Note:** You should use powerful colors, i've experienced that bright colors tend to look like white on the ring.

### /Your/Topic/Tree/Status
This topic is basically used as a "reminder flag" for the Rememberall. You can use the button on the Rememberall to acknowledge the current event (short press on the button), where the following will happen:
* Rememberall sets the `Status` topic to `ack` (retained)
* Rememberall stops reminding by disabling the LED ring and clearing the display
* Rememberall will go to sleep for `WIFI_SLEEP_DURATION` (30 minutes by default)

## Configuration
Beside the basic build / flash configuration described in the [PIO-ESP32-Template README](https://github.com/juepi/PIO-ESP32-Template), you will need to configure:

### `include/time-config.h`
Setup your desired NTP server as `NTPServer1`, optionally add a second one.

### `include/user-config.h`
This file contains all configurable options for this project like
* type/wiring of ePaper display
* type/wiring of LED strip/ring/whatever
* default WiFi sleep time
* MQTT settings
* FastLED animation settings and global brightness for reminders

The file should be well commented.

## The Feeder Script
The PoSh feeder script is designed to be run as a scheduled task once every hour (preferrable at 0 minutes). The script is (hopefully) well documented and should be adopted for your needs in the `Configuration Settings` section. It will handle regular and recurring events, filter the first event from all configured calendars and parse it for the Rememberall according to your configuration.  
Note that it requires 2 external libraries for MQTT communication and iCalendar handling:

* [IcalVCard](https://afterlogic.com/mailbee-net/icalvcard) / [NuGet package](https://www.nuget.org/packages/ICalVCard)
* [M2Mqtt](https://github.com/eclipse/paho.mqtt.m2mqtt) / [NuGet package](https://www.nuget.org/packages/M2Mqtt/)

Place the 2 DLL files in a `lib` subdirectory of the place where the feeder script resides.  
**Note:** In order to make the german "umlauts conversion" work correctly, you need to make sure that the script is saved **UTF8-BOM** encoded locally.

## Pushbutton functions
The (optional) pushbutton has 2 functions:
* Short Press: acknowledge the current event as described above
* Long Press: Toggle WiFi up/down (useful e.g. for OTA-flashing)

Note that the pushbutton cannot wake the ESP while in DeepSleep, so the button will only react during active reminding periods.

## Photos
Here are 2 photos from my Rememberall:

Front             |  Back
:-------------------------:|:-------------------------:
![Front](pics/Rememberall_front.jpg?raw=true) | ![Back](pics/Rememberall_back.jpg?raw=true)

## Changelog

### v1.0.1
- Minor code changes (final MQTT topic tree, external button)
- Feeder script encoding fixed
- Added fotos of finalized hardware
- Updated Readme

### v1.0.2
- Bugfix: WiFi did not wake up after `WIFI_SLEEP_DURATION`
- Updated readme with pushbutton functionality

### v1.0.3
- Improved Feeder script; now adds multiple words (one word per ePaper line) from the summary field of the event (up to 3 lines which can be showed on the ePaper display)
- Feeder script has a new "WhatIf" switch parameter for testing (prints what would be sent to the MQTT broker)
  

Have fun,  
Juergen