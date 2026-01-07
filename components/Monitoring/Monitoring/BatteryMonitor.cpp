/**
 * @file BatteryMonitor.cpp
 * @brief Business Logic Layer - Battery monitoring implementation
 *
 * Platform-independent battery monitoring logic.
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 */

#include "BatteryMonitor.hpp"
#include <esp_log.h>

static const char* TAG = "[BatteryMonitor]";

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

float BatteryMonitor::voltageToPercentage(int voltage_mv)
{
    const float volts = static_cast<float>(voltage_mv);

    // Handle boundary conditions
    if (volts >= soc_lookup_.front().voltage_mv)
        return soc_lookup_.front().soc;

    if (volts <= soc_lookup_.back().voltage_mv)
        return soc_lookup_.back().soc;

    // Linear interpolation between lookup table points
    for (size_t i = 0; i < soc_lookup_.size() - 1; ++i)
    {
        const auto& high = soc_lookup_[i];
        const auto& low = soc_lookup_[i + 1];

        if (volts <= high.voltage_mv && volts >= low.voltage_mv)
        {
            const float voltage_span = high.voltage_mv - low.voltage_mv;
            if (voltage_span <= 0.0f)
            {
                return low.soc;
            }
            const float ratio = (volts - low.voltage_mv) / voltage_span;
            return low.soc + ratio * (high.soc - low.soc);
        }
    }

    return 0.0f;
}

BatteryStatus BatteryMonitor::getBatteryStatus() const
{
    BatteryStatus status = {0, 0.0f, false};

#if CONFIG_MONITORING_BATTERY_ENABLE
    const int mv = getBatteryMilliVolts();
    if (mv <= 0)
        return status;

    status.voltage_mv = mv;
    status.percentage = std::clamp(voltageToPercentage(mv), 0.0f, 100.0f);
    status.valid = true;
#endif

    return status;
}
