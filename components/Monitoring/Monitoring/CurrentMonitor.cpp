/**
 * @file CurrentMonitor.cpp
 * @brief Business Logic Layer - Current monitoring implementation
 *
 * Platform-independent current monitoring logic.
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 */

#include "CurrentMonitor.hpp"
#include <esp_log.h>

static const char *TAG = "[CurrentMonitor]";

void CurrentMonitor::setup()
{
#ifdef CONFIG_MONITORING_LED_CURRENT
    if (!AdcSampler::isSupported())
    {
        ESP_LOGI(TAG, "LED current monitoring not supported on this target");
        return;
    }

    const bool ok = adc_.init(CONFIG_MONITORING_LED_ADC_GPIO, ADC_ATTEN_DB_12, ADC_BITWIDTH_DEFAULT, CONFIG_MONITORING_LED_SAMPLES);
    if (!ok)
    {
        ESP_LOGE(TAG, "ADC init failed for LED current monitor");
        return;
    }

    ESP_LOGI(TAG, "LED current monitor enabled (GPIO=%d, Shunt=%dmΩ, Gain=%d)", CONFIG_MONITORING_LED_ADC_GPIO, CONFIG_MONITORING_LED_SHUNT_MILLIOHM,
             CONFIG_MONITORING_LED_GAIN);
#else
    ESP_LOGI(TAG, "LED current monitoring disabled by Kconfig");
#endif
}

float CurrentMonitor::getCurrentMilliAmps() const
{
#ifdef CONFIG_MONITORING_LED_CURRENT
    if (!AdcSampler::isSupported())
        return 0.0f;

    const int shunt_milliohm = CONFIG_MONITORING_LED_SHUNT_MILLIOHM; // mΩ
    if (shunt_milliohm <= 0)
        return 0.0f;

    if (!adc_.sampleOnce())
        return 0.0f;

    int filtered_mv = adc_.getFilteredMilliVolts();

    // Apply gain compensation if using current sense amplifier
    if (CONFIG_MONITORING_LED_GAIN > 0)
        filtered_mv = filtered_mv / CONFIG_MONITORING_LED_GAIN;  // convert back to shunt voltage

    // Physically correct scaling:
    // I[mA] = 1000 * Vshunt[mV] / R[mΩ]
    return (1000.0f * static_cast<float>(filtered_mv)) / static_cast<float>(shunt_milliohm);
#else
    return 0.0f;
#endif
}