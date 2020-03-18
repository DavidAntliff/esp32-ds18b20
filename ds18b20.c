/*
 * MIT License
 *
 * Copyright (c) 2017-2019 David Antliff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file ds18b20.c
 *
 * Resolution is cached in the DS18B20_Info object to avoid querying the hardware
 * every time a temperature conversion is required. However this can result in the
 * cached value becoming inconsistent with the hardware value, so care must be taken.
 *
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"

#include "ds18b20.h"
#include "owb.h"

static const char * TAG = "ds18b20";
static const int T_CONV = 750;   // maximum conversion time at 12-bit resolution in milliseconds

// Function commands
#define DS18B20_FUNCTION_TEMP_CONVERT       0x44
#define DS18B20_FUNCTION_SCRATCHPAD_WRITE   0x4E
#define DS18B20_FUNCTION_SCRATCHPAD_READ    0xBE
#define DS18B20_FUNCTION_SCRATCHPAD_COPY    0x48
#define DS18B20_FUNCTION_EEPROM_RECALL      0xB8
#define DS18B20_FUNCTION_POWER_SUPPLY_READ  0xB4

/// @cond ignore
typedef struct
{
    uint8_t temperature[2];    // [0] is LSB, [1] is MSB
    uint8_t trigger_high;
    uint8_t trigger_low;
    uint8_t configuration;
    uint8_t reserved[3];
    uint8_t crc;
} __attribute__((packed)) Scratchpad;
/// @endcond ignore

static void _init(DS18B20_Info * ds18b20_info, const OneWireBus * bus)
{
    if (ds18b20_info != NULL)
    {
        ds18b20_info->bus = bus;
        memset(&ds18b20_info->rom_code, 0, sizeof(ds18b20_info->rom_code));
        ds18b20_info->use_crc = false;
        ds18b20_info->resolution = DS18B20_RESOLUTION_INVALID;
        ds18b20_info->solo = false;   // assume multiple devices unless told otherwise
        ds18b20_info->init = true;
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

static bool _is_init(const DS18B20_Info * ds18b20_info)
{
    bool ok = false;
    if (ds18b20_info != NULL)
    {
        if (ds18b20_info->init)
        {
            // OK
            ok = true;
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20_info is not initialised");
        }
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
    return ok;
}

static bool _address_device(const DS18B20_Info * ds18b20_info)
{
    bool present = false;
    if (_is_init(ds18b20_info))
    {
        owb_reset(ds18b20_info->bus, &present);
        if (present)
        {
            if (ds18b20_info->solo)
            {
                // if there's only one device on the bus, we can skip
                // sending the ROM code and instruct it directly
                owb_write_byte(ds18b20_info->bus, OWB_ROM_SKIP);
            }
            else
            {
                // if there are multiple devices on the bus, a Match ROM command
                // must be issued to address a specific slave
                owb_write_byte(ds18b20_info->bus, OWB_ROM_MATCH);
                owb_write_rom_code(ds18b20_info->bus, ds18b20_info->rom_code);
            }
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20 device not responding");
        }
    }
    return present;
}

static bool _check_resolution(DS18B20_RESOLUTION resolution)
{
    return (resolution >= DS18B20_RESOLUTION_9_BIT) && (resolution <= DS18B20_RESOLUTION_12_BIT);
}

static float _wait_for_conversion(const DS18B20_Info * ds18b20_info)
{
    float elapsed_time = 0.0f;
    if (_check_resolution(ds18b20_info->resolution))
    {
        int divisor = 1 << (DS18B20_RESOLUTION_12_BIT - ds18b20_info->resolution);

        // allow for 10% overtime
        float max_conversion_time = (float)T_CONV / (float)divisor * 1.1;
        int max_conversion_ticks = ceil(max_conversion_time / portTICK_PERIOD_MS);
        ESP_LOGD(TAG, "wait for conversion: max %.0f ms, %d ticks", max_conversion_time, max_conversion_ticks);

        // wait for conversion to complete
        TickType_t start_ticks = xTaskGetTickCount();
        TickType_t duration_ticks = 0;
        uint8_t status = 0;
        do
        {
            vTaskDelay(1);
            owb_read_bit(ds18b20_info->bus, &status);
            duration_ticks = xTaskGetTickCount() - start_ticks;
        } while (status == 0 && duration_ticks < max_conversion_ticks);

        elapsed_time = duration_ticks * portTICK_PERIOD_MS;
        if (duration_ticks >= max_conversion_ticks)
        {
            ESP_LOGW(TAG, "conversion timed out");
        }
        else
        {
            ESP_LOGD(TAG, "conversion took at most %.0f ms", elapsed_time);
        }
    }
    return elapsed_time;
}

static float _decode_temp(uint8_t lsb, uint8_t msb, DS18B20_RESOLUTION resolution)
{
    float result = 0.0f;
    if (_check_resolution(resolution))
    {
        // masks to remove undefined bits from result
        static const uint8_t lsb_mask[4] = { ~0x07, ~0x03, ~0x01, ~0x00 };
        uint8_t lsb_masked = lsb_mask[resolution - DS18B20_RESOLUTION_9_BIT] & lsb;
        int16_t raw = (msb << 8) | lsb_masked;
        result = raw / 16.0f;
    }
    else
    {
        ESP_LOGE(TAG, "Unsupported resolution %d", resolution);
    }
    return result;
}

static size_t _min(size_t x, size_t y)
{
    return x > y ? y : x;
}

static DS18B20_ERROR _read_scratchpad(const DS18B20_Info * ds18b20_info, Scratchpad * scratchpad, size_t count)
{
    // If CRC is enabled, regardless of count, read the entire scratchpad and verify the CRC,
    // otherwise read up to the scratchpad size, or count, whichever is smaller.

    if (!scratchpad) {
        return DS18B20_ERROR_NULL;
    }

    DS18B20_ERROR err = DS18B20_ERROR_UNKNOWN;

    if (ds18b20_info->use_crc)
    {
        count = sizeof(Scratchpad);
    }
    count = _min(sizeof(Scratchpad), count);   // avoid reading past end of scratchpad

    ESP_LOGD(TAG, "scratchpad read: CRC %d, count %d", ds18b20_info->use_crc, count);
    if (_address_device(ds18b20_info))
    {
        // read scratchpad
        if (owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_SCRATCHPAD_READ) == OWB_STATUS_OK)
        {
            if (owb_read_bytes(ds18b20_info->bus, (uint8_t *)scratchpad, count) == OWB_STATUS_OK)
            {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, scratchpad, count, ESP_LOG_DEBUG);

                err = DS18B20_OK;
                if (!ds18b20_info->use_crc)
                {
                    // Without CRC, or partial read:
                    ESP_LOGD(TAG, "No CRC check");
                    bool is_present = false;
                    owb_reset(ds18b20_info->bus, &is_present);  // terminate early
                }
                else
                {
                    // With CRC:
                    if (owb_crc8_bytes(0, (uint8_t *)scratchpad, sizeof(*scratchpad)) != 0)
                    {
                        ESP_LOGE(TAG, "CRC failed");
                        err = DS18B20_ERROR_CRC;
                    }
                    else
                    {
                        ESP_LOGD(TAG, "CRC ok");
                    }
                }
            }
            else
            {
                ESP_LOGE(TAG, "owb_read_bytes failed");
                err = DS18B20_ERROR_OWB;
            }
        }
        else
        {
            ESP_LOGE(TAG, "owb_write_byte failed");
            err = DS18B20_ERROR_OWB;
        }
    }
    return err;
}

static bool _write_scratchpad(const DS18B20_Info * ds18b20_info, const Scratchpad * scratchpad, bool verify)
{
    bool result = false;
    // Only bytes 2, 3 and 4 (trigger and configuration) can be written.
    // All three bytes MUST be written before the next reset to avoid corruption.
    if (_is_init(ds18b20_info))
    {
        if (_address_device(ds18b20_info))
        {
            ESP_LOGD(TAG, "scratchpad write 3 bytes:");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, &scratchpad->trigger_high, 3, ESP_LOG_DEBUG);
            owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_SCRATCHPAD_WRITE);
            owb_write_bytes(ds18b20_info->bus, (uint8_t *)&scratchpad->trigger_high, 3);
            result = true;

            if (verify)
            {
                Scratchpad read = {0};
                if (_read_scratchpad(ds18b20_info, &read, offsetof(Scratchpad, configuration) + 1) == DS18B20_OK)
                {
                    if (memcmp(&scratchpad->trigger_high, &read.trigger_high, 3) != 0)
                    {
                        ESP_LOGE(TAG, "scratchpad verify failed: "
                                 "wrote {0x%02x, 0x%02x, 0x%02x}, "
                                 "read {0x%02x, 0x%02x, 0x%02x}",
                                 scratchpad->trigger_high, scratchpad->trigger_low, scratchpad->configuration,
                                 read.trigger_high, read.trigger_low, read.configuration);
                        result = false;
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "read scratchpad failed");
                    result = false;
                }
            }
        }
    }
    return result;
}


// Public API

DS18B20_Info * ds18b20_malloc(void)
{
    DS18B20_Info * ds18b20_info = malloc(sizeof(*ds18b20_info));
    if (ds18b20_info != NULL)
    {
        memset(ds18b20_info, 0, sizeof(*ds18b20_info));
        ESP_LOGD(TAG, "malloc %p", ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "malloc failed");
    }

    return ds18b20_info;
}

void ds18b20_free(DS18B20_Info ** ds18b20_info)
{
    if (ds18b20_info != NULL && (*ds18b20_info != NULL))
    {
        ESP_LOGD(TAG, "free %p", *ds18b20_info);
        free(*ds18b20_info);
        *ds18b20_info = NULL;
    }
}

void ds18b20_init(DS18B20_Info * ds18b20_info, const OneWireBus * bus, OneWireBus_ROMCode rom_code)
{
    if (ds18b20_info != NULL)
    {
        _init(ds18b20_info, bus);
        ds18b20_info->rom_code = rom_code;

        // read current resolution from device as it may not be power-on or factory default
        ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

void ds18b20_init_solo(DS18B20_Info * ds18b20_info, const OneWireBus * bus)
{
    if (ds18b20_info != NULL)
    {
        _init(ds18b20_info, bus);
        ds18b20_info->solo = true;
        // ROM code not required

        // read current resolution from device as it may not be power-on or factory default
        ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

void ds18b20_use_crc(DS18B20_Info * ds18b20_info, bool use_crc)
{
    if (_is_init(ds18b20_info))
    {
        ds18b20_info->use_crc = use_crc;
        ESP_LOGD(TAG, "use_crc %d", ds18b20_info->use_crc);
    }
}

bool ds18b20_set_resolution(DS18B20_Info * ds18b20_info, DS18B20_RESOLUTION resolution)
{
    bool result = false;
    if (_is_init(ds18b20_info))
    {
        if (_check_resolution(ds18b20_info->resolution))
        {
            // read scratchpad up to and including configuration register
            Scratchpad scratchpad = {0};
            _read_scratchpad(ds18b20_info, &scratchpad,
                    offsetof(Scratchpad, configuration) - offsetof(Scratchpad, temperature) + 1);

            // modify configuration register to set resolution
            uint8_t value = (((resolution - 1) & 0x03) << 5) | 0x1f;
            scratchpad.configuration = value;
            ESP_LOGD(TAG, "configuration value 0x%02x", value);

            // write bytes 2, 3 and 4 of scratchpad
            result = _write_scratchpad(ds18b20_info, &scratchpad, /* verify */ true);
            if (result)
            {
                ds18b20_info->resolution = resolution;
                ESP_LOGD(TAG, "Resolution set to %d bits", (int)resolution);
            }
            else
            {
                // Resolution change failed - update the info resolution with the value read from configuration
                ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
                ESP_LOGW(TAG, "Resolution consistency lost - refreshed from device: %d", ds18b20_info->resolution);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Unsupported resolution %d", resolution);
        }
    }
    return result;
}

DS18B20_RESOLUTION ds18b20_read_resolution(DS18B20_Info * ds18b20_info)
{
    DS18B20_RESOLUTION resolution = DS18B20_RESOLUTION_INVALID;
    if (_is_init(ds18b20_info))
    {
        // read scratchpad up to and including configuration register
        Scratchpad scratchpad = {0};
        _read_scratchpad(ds18b20_info, &scratchpad,
                offsetof(Scratchpad, configuration) - offsetof(Scratchpad, temperature) + 1);

        resolution = ((scratchpad.configuration >> 5) & 0x03) + DS18B20_RESOLUTION_9_BIT;
        if (!_check_resolution(resolution))
        {
            ESP_LOGE(TAG, "invalid resolution read from device: 0x%02x", scratchpad.configuration);
            resolution = DS18B20_RESOLUTION_INVALID;
        }
        else
        {
            ESP_LOGD(TAG, "Resolution read as %d", resolution);
        }
    }
    return resolution;
}

bool ds18b20_convert(const DS18B20_Info * ds18b20_info)
{
    bool result = false;
    if (_is_init(ds18b20_info))
    {
        const OneWireBus * bus = ds18b20_info->bus;
        if (_address_device(ds18b20_info))
        {
            // initiate a temperature measurement
            owb_write_byte(bus, DS18B20_FUNCTION_TEMP_CONVERT);
            result = true;
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20 device not responding");
        }
    }
    return result;
}

void ds18b20_convert_all(const OneWireBus * bus)
{
    bool is_present = false;
    owb_reset(bus, &is_present);
    owb_write_byte(bus, OWB_ROM_SKIP);
    owb_write_byte(bus, DS18B20_FUNCTION_TEMP_CONVERT);
}

float ds18b20_wait_for_conversion(const DS18B20_Info * ds18b20_info)
{
    float elapsed_time = 0.0f;
    if (_is_init(ds18b20_info))
    {
        elapsed_time = _wait_for_conversion(ds18b20_info);
    }
    return elapsed_time;
}

DS18B20_ERROR ds18b20_read_temp(const DS18B20_Info * ds18b20_info, float * value)
{
    DS18B20_ERROR err = DS18B20_ERROR_UNKNOWN;
    if (_is_init(ds18b20_info))
    {
        uint8_t temp_LSB = 0x00;
        uint8_t temp_MSB = 0x80;
        Scratchpad scratchpad = {0};
        if ((err = _read_scratchpad(ds18b20_info, &scratchpad, 2)) == DS18B20_OK)
        {
            temp_LSB = scratchpad.temperature[0];
            temp_MSB = scratchpad.temperature[1];
        }

        float temp = _decode_temp(temp_LSB, temp_MSB, ds18b20_info->resolution);
        ESP_LOGD(TAG, "temp_LSB 0x%02x, temp_MSB 0x%02x, temp %f", temp_LSB, temp_MSB, temp);

        if (value)
        {
            *value = temp;
        }
    }
    return err;
}

DS18B20_ERROR ds18b20_convert_and_read_temp(const DS18B20_Info * ds18b20_info, float * value)
{
    DS18B20_ERROR err = DS18B20_ERROR_UNKNOWN;
    if (_is_init(ds18b20_info))
    {
        if (ds18b20_convert(ds18b20_info))
        {
            // wait at least maximum conversion time
            _wait_for_conversion(ds18b20_info);
            if (value)
            {
                *value = 0.0f;
                err = ds18b20_read_temp(ds18b20_info, value);
            }
            else
            {
                err = DS18B20_ERROR_NULL;
            }
        }
    }
    return err;
}


