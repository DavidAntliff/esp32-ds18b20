# esp32-ds18b20

## Introduction

This is a ESP32-compatible C component for the Maxim Integrated DS18B20 Programmable Resolution 1-Wire Digital
Thermometer device.

It supports multiple devices on the same 1-Wire bus.

It is tested with v4.4.4 and v5.0.1 of the [ESP-IDF](https://github.com/espressif/esp-idf) environment.

## Dependencies

Requires [esp32-owb](https://github.com/DavidAntliff/esp32-owb).

## Example

See [esp32-ds18b20-example](https://github.com/DavidAntliff/esp32-ds18b20-example) for an example that supports single
and multiple devices on a single bus.

## Features

In cooperation with the underlying esp32-owb component, this component includes:

 * External power supply mode.
 * Parasitic power mode (VDD and GND connected) - see notes below.
 * Static (stack-based) or dynamic (malloc-based) memory model.
 * No globals - support any number of DS18B20 devices on any number of 1-Wire buses simultaneously.
 * 1-Wire device detection and validation, including search for multiple devices on a single bus.
 * Addressing optimisation for a single (solo) device on a bus.
 * CRC checks on temperature data.
 * Programmable temperature measurement resolution (9, 10, 11 or 12-bit resolution).
 * Temperature conversion and retrieval.
 * Separation of conversion and temperature retrieval to allow for simultaneous conversion across multiple devices.

## Parasitic Power Mode

Consult the [datasheet](http://datasheets.maximintegrated.com/en/ds/DS18B20.pdf) for more detailed information about
Parasitic Power mode.

Parasitic power operation can be detected by `ds18b20_check_for_parasite_power()` followed by a call to
`owb_use_parasitic_power()`, or simply set explicitly by a call to the latter.

This library has been tested on the ESP32 with two parasitic-power configurations, with two DS18B20 devices at 3.3V:

1. Disconnect power to each device's VDD pin, and connect that pin to GND for each device. Power is supplied to
   each device through the OWB data line.
2. Connect the OWB data line to VCC via a P-channel MOSFET (e.g. BS250) and current-limiting resistor (e.g. 270 Ohm).
   Ensure your code calls `owb_use_strong_pullup_gpio()` with the GPIO connected to the MOSFET Gate. The GPIO will go
   high during temperature conversion, turning on the MOSFET and providing power from VCC to each device through the OWB
   data line.

If you set up configuration 1 and do not see CRC errors, but you get incorrect temperature readings around 80 - 85 
degrees C, then it is likely that your DS18B20 devices are running out of power during the temperature conversion. In 
this case, consider reducing the value of the pull-up resistor. For example, I have had success obtaining correct
conversions from two parasitic DS18B20 devices by replacing the 4k7 Ohm pull-up resistor with a 1 kOhm resistor.

Alternatively, consider using configuration 2.

Note that use of parasitic power mode disables the ability for devices on the bus to signal that an operation has 
completed. This means that DS18B20 devices in parasitic power mode are not able to communicate when they have completed
a temperature conversion. In this mode, a delay for a pre-calculated duration occurs, and then the conversion result is
read from the device(s). *If your ESP32 is not running on the correct clock rate, this duration may be too short!*  

## Documentation

Automatically generated API documentation (doxygen) is available [here](https://davidantliff.github.io/esp32-ds18b20/index.html).

## Source Code

The source is available from [GitHub](https://www.github.com/DavidAntliff/esp32-ds18b20).

## License

The code in this project is licensed under the MIT license - see LICENSE for details.

## Links

 * [DS18B20 Datasheet](http://datasheets.maximintegrated.com/en/ds/DS18B20.pdf)
 * [1-Wire Communication Through Software](https://www.maximintegrated.com/en/app-notes/index.mvp/id/126)
 * [1-Wire Search Algorithm](https://www.maximintegrated.com/en/app-notes/index.mvp/id/187)
 * [Espressif IoT Development Framework for ESP32](https://github.com/espressif/esp-idf)

## Acknowledgements

Parts of this code are based on references provided to the public domain by Maxim Integrated.

"1-Wire" is a registered trademark of Maxim Integrated.

## Roadmap

The following features are anticipated but not yet implemented:

 * Concurrency support (multiple tasks accessing devices on the same bus).
 * Alarm support.
 * EEPROM support.
 * Parasitic power support.
