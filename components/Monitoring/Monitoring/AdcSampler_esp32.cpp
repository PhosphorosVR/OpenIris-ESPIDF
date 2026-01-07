/**
 * @file AdcSampler_esp32.cpp
 * @brief BSP Layer - ESP32 specific GPIO to ADC channel mapping
 *
 * ESP32 ADC1 GPIO mapping:
 * - GPIO32 → ADC1_CH4
 * - GPIO33 → ADC1_CH5
 * - GPIO34 → ADC1_CH6
 * - GPIO35 → ADC1_CH7
 * - GPIO36 → ADC1_CH0
 * - GPIO37 → ADC1_CH1
 * - GPIO38 → ADC1_CH2
 * - GPIO39 → ADC1_CH3
 *
 * Note: ADC2 is not used to avoid conflicts with Wi-Fi.
 */

#include "AdcSampler.hpp"

#if defined(CONFIG_IDF_TARGET_ESP32)

bool AdcSampler::map_gpio_to_channel(int gpio, adc_unit_t& unit, adc_channel_t& channel)
{
    unit = ADC_UNIT_1;  // Only use ADC1 to avoid Wi-Fi conflict

    // ESP32: ADC1 GPIO mapping (GPIO32-39)
    switch (gpio)
    {
    case 36:
        channel = ADC_CHANNEL_0;
        return true;
    case 37:
        channel = ADC_CHANNEL_1;
        return true;
    case 38:
        channel = ADC_CHANNEL_2;
        return true;
    case 39:
        channel = ADC_CHANNEL_3;
        return true;
    case 32:
        channel = ADC_CHANNEL_4;
        return true;
    case 33:
        channel = ADC_CHANNEL_5;
        return true;
    case 34:
        channel = ADC_CHANNEL_6;
        return true;
    case 35:
        channel = ADC_CHANNEL_7;
        return true;
    default:
        channel = ADC_CHANNEL_0;
        return false;
    }
}

#endif  // CONFIG_IDF_TARGET_ESP32
