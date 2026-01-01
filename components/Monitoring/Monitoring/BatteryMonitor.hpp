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

#include <algorithm>
#include <array>
#include <cmath>
#include "AdcSampler.hpp"
#include "sdkconfig.h"


/**
 * @struct BatteryStatus
 * @brief Battery status information
 */
struct BatteryStatus
{
    int voltage_mv;    // Battery voltage in millivolts
    float percentage;  // State of charge percentage (0-100%)
    bool valid;        // Whether the reading is valid
};

/**
 * @class BatteryMonitor
 * @brief Monitors battery voltage and calculates state of charge for Li-ion batteries
 *
 * Uses AdcSampler (BSP layer) for hardware abstraction.
 * Includes voltage-to-SOC lookup table for typical Li-ion/Li-Po batteries.
 *
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

    /**
     * @brief Read battery voltage (with divider compensation)
     * @return Battery voltage in millivolts, 0 on failure
     */
    int getBatteryMilliVolts() const;

    /**
     * @brief Calculate battery state of charge from voltage
     * @param voltage_mv Battery voltage in millivolts
     * @return State of charge percentage (0-100%)
     */
    static float voltageToPercentage(int voltage_mv);

    /**
     * @brief Get complete battery status (voltage + percentage)
     * @return BatteryStatus struct with voltage, percentage, and validity
     */
    BatteryStatus getBatteryStatus() const;

    /**
     * @brief Check if battery monitoring is enabled and supported
     * @return true if enabled and ADC is supported
     */
    static constexpr bool isEnabled()
    {
#ifdef CONFIG_MONITORING_BATTERY_ENABLE
        return AdcSampler::isSupported();
#else
        return false;
#endif
    }

private:
    /**
     * @brief Li-ion/Li-Po voltage to SOC lookup table entry
     */
    struct VoltageSOC
    {
        float voltage_mv;
        float soc;
    };

    /**
     * @brief Typical Li-ion single cell discharge curve lookup table
     * Based on typical 3.7V nominal Li-ion/Li-Po cell characteristics
     */
    static constexpr std::array<VoltageSOC, 12> soc_lookup_ = {{
        {4200.0f, 100.0f}, // Fully charged
        {4060.0f, 90.0f},
        {3980.0f, 80.0f},
        {3920.0f, 70.0f},
        {3870.0f, 60.0f},
        {3820.0f, 50.0f},
        {3790.0f, 40.0f},
        {3770.0f, 30.0f},
        {3740.0f, 20.0f},
        {3680.0f, 10.0f},
        {3450.0f, 5.0f}, // Low battery warning
        {3300.0f, 0.0f}, // Empty / cutoff voltage
    }};

    float scale_{1.0f};      // Voltage divider scaling factor
    mutable AdcSampler adc_; // ADC sampler instance (BSP layer)
};
