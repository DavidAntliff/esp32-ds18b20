# esp32-ds18b20

## Introduction

This is a ESP32-compatible C library for the Maxim Integrated DS18B20 Programmable Resolution 1-Wire Digital Thermometer device.

It supports multiple devices on the same 1-Wire bus.

It is written and tested for the [ESP-IDF](https://github.com/espressif/esp-idf) environment, using the xtensa-esp32-elf toolchain (gcc version 5.2.0).

## Dependencies

Requires [esp32-owb](https://github.com/DavidAntliff/esp32-owb).

## Example

See [esp32-ds18b20-example](https://github.com/DavidAntliff/esp32-ds18b20-example) for a working multi-device example.

## Features

In cooperation with the underlying esp32-owb component, this component includes:

 * External power supply mode (parasitic mode not yet supported).
 * Static (stack-based) or dynamic (malloc-based) memory model.
 * No globals - support any number of DS18B20 devices on any number of 1-Wire buses simultaneously.
 * 1-Wire device detection and validation, including search for multiple devices on a single bus.
 * Addressing optimisation for a single (solo) device on a bus.
 * CRC checks on temperature data.
 * Programmable temperature measurement resolution (9, 10, 11 or 12-bit resolution).
 * Temperature conversion and retrieval.
 * Separation of conversion and temperature retrieval to allow for simultaneous conversion across multiple devices.

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
