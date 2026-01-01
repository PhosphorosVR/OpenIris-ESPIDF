#pragma once
/**
 * @file BatteryMonitor.hpp
 * @brief Business Logic Layer - Battery monitoring (voltage, capacity, health)
 *
 * Architecture:
 * +-----------------------+
 * |   MonitoringManager   | ← High-level coordinator
 * +-----------------------+
 * |  BatteryMonitor       | ← Battery logic (this file)
 * |  CurrentMonitor       | ← Current logic
 * +-----------------------+
 * |      AdcSampler       | ← BSP: Unified ADC sampling interface
 * +-----------------------+
 */

#include "AdcSampler.hpp"
#include "sdkconfig.h"
#include <cmath>

/**
 * @class BatteryMonitor
 * @brief Monitors battery voltage through a resistor divider
 *
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 * Configuration is done via Kconfig options:
 * - CONFIG_MONITORING_BATTERY_ENABLE
 * - CONFIG_MONITORING_BATTERY_ADC_GPIO
 * - CONFIG_MONITORING_BATTERY_DIVIDER_R_TOP_OHM
 * - CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM
 * - CONFIG_MONITORING_BATTERY_SAMPLES
 */
class BatteryMonitor
{
public:
    BatteryMonitor() = default;
    ~BatteryMonitor() = default;

    // Initialize battery monitoring hardware
    bool setup();

    // Read once, update filter, and return battery voltage in mV (after divider compensation), 0 on failure
    int getBatteryMilliVolts() const;

    // Whether monitoring is enabled by Kconfig and supported by BSP
    static constexpr bool isEnabled()
    {
#ifdef CONFIG_MONITORING_BATTERY_ENABLE
        return AdcSampler::isSupported();
#else
        return false;
#endif
    }

private:
    float scale_{1.0f}; // Voltage divider scaling factor
    mutable AdcSampler adc_; // ADC sampler instance (BSP layer)
};
