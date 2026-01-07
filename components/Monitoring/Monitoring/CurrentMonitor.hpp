#ifndef CURRENT_MONITOR_HPP
#define CURRENT_MONITOR_HPP
#pragma once
/**
 * @file CurrentMonitor.hpp
 * @brief Business Logic Layer - Current monitoring (power, instantaneous current)
 *
 * Architecture:
 * +-----------------------+
 * |   MonitoringManager   | ← High-level coordinator
 * +-----------------------+
 * |  BatteryMonitor       | ← Battery logic
 * |  CurrentMonitor       | ← Current logic (this file)
 * +-----------------------+
 * |      AdcSampler       | ← BSP: Unified ADC sampling interface
 * +-----------------------+
 */

#include <cstdint>
#include "AdcSampler.hpp"
#include "sdkconfig.h"

/**
 * @class CurrentMonitor
 * @brief Monitors LED current through a shunt resistor
 *
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 * Configuration is done via Kconfig options:
 * - CONFIG_MONITORING_LED_CURRENT
 * - CONFIG_MONITORING_LED_ADC_GPIO
 * - CONFIG_MONITORING_LED_SHUNT_MILLIOHM
 * - CONFIG_MONITORING_LED_GAIN
 * - CONFIG_MONITORING_LED_SAMPLES
 */
class CurrentMonitor
{
   public:
    CurrentMonitor() = default;
    ~CurrentMonitor() = default;

    // Initialize current monitoring hardware
    void setup();

    // convenience: combined sampling and compute; returns mA
    float getCurrentMilliAmps() const;

    // Whether monitoring is enabled by Kconfig and supported by BSP
    static constexpr bool isEnabled()
    {
#if CONFIG_MONITORING_LED_CURRENT
        return AdcSampler::isSupported();
#else
        return false;
#endif
    }

   private:
    mutable AdcSampler adc_;  // ADC sampler instance (BSP layer)
};

#endif
