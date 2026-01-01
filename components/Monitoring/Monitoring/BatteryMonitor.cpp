/**
 * @file BatteryMonitor.cpp
 * @brief Business Logic Layer - Battery monitoring implementation
 *
 * Platform-independent battery monitoring logic.
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 */

#include "BatteryMonitor.hpp"
#include <esp_log.h>

static const char *TAG = "[BatteryMonitor]";

bool BatteryMonitor::setup()
{
#if CONFIG_MONITORING_BATTERY_ENABLE
    if (!AdcSampler::isSupported())
    {
        ESP_LOGI(TAG, "Battery monitoring not supported on this target");
        return false;
    }

    // Validate divider resistor configuration
    if (CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM == 0)
    {
        ESP_LOGE(TAG, "Invalid divider bottom resistor: %d", CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM);
        return false;
    }
    if (CONFIG_MONITORING_BATTERY_DIVIDER_R_TOP_OHM <= 0 || CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM < 0)
    {
        scale_ = 1.0f;
    }
    else
    {
        // Calculate voltage divider scaling factor
        // Vbat = Vadc * (R_TOP + R_BOTTOM) / R_BOTTOM
        scale_ = 1.0f + static_cast<float>(CONFIG_MONITORING_BATTERY_DIVIDER_R_TOP_OHM) / static_cast<float>(CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM);
    }

    // Initialize ADC sampler (BSP layer)
    if (!adc_.init(CONFIG_MONITORING_BATTERY_ADC_GPIO, ADC_ATTEN_DB_12, ADC_BITWIDTH_DEFAULT, CONFIG_MONITORING_BATTERY_SAMPLES))
    {
        ESP_LOGE(TAG, "Battery ADC init failed");
        return false;
    }
    ESP_LOGI(TAG, "Battery monitor enabled (GPIO=%d, scale=%.3f)", CONFIG_MONITORING_BATTERY_ADC_GPIO, scale_);
    return true;
#else
    ESP_LOGI(TAG, "Battery monitoring disabled by Kconfig");
    return false;
#endif
}

int BatteryMonitor::getBatteryMilliVolts() const
{
#if CONFIG_MONITORING_BATTERY_ENABLE
    if (!AdcSampler::isSupported())
        return 0;

    if (!adc_.sampleOnce())
        return 0;

    const int mv_at_adc = adc_.getFilteredMilliVolts();
    if (mv_at_adc <= 0)
        return 0;

    // Apply voltage divider scaling
    const float battery_mv = static_cast<float>(mv_at_adc) * scale_;
    return static_cast<int>(std::lround(battery_mv));
#else
    return 0;
#endif
}
