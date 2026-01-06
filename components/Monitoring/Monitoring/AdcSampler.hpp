#pragma once
/**
 * @file AdcSampler.hpp
 * @brief BSP Layer - Unified ADC sampling interface (Hardware Abstraction)
 *
 * Architecture:
 * +-----------------------+
 * |   MonitoringManager   | ← High-level coordinator
 * +-----------------------+
 * |  BatteryMonitor       | ← Battery logic: voltage, capacity, health
 * |  CurrentMonitor       | ← Current logic: power, instantaneous current
 * +-----------------------+
 * |      AdcSampler       | ← BSP: Unified ADC sampling interface (this file)
 * +-----------------------+
 * |   ESP-IDF ADC HAL     | ← Espressif official driver
 * +-----------------------+
 */

#include <cstddef>
#include <cstdint>
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32)
#include <vector>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

/**
 * @class AdcSampler
 * @brief Hardware abstraction layer for ADC sampling with moving average filter
 *
 * This class provides a unified interface for ADC sampling across different
 * ESP32 variants. Platform-specific GPIO-to-channel mapping is handled internally.
 */
class AdcSampler
{
   public:
    AdcSampler() = default;
    ~AdcSampler();

    // Non-copyable, non-movable (owns hardware resources)
    AdcSampler(const AdcSampler&) = delete;
    AdcSampler& operator=(const AdcSampler&) = delete;
    AdcSampler(AdcSampler&&) = delete;
    AdcSampler& operator=(AdcSampler&&) = delete;

    /**
     * @brief Initialize the ADC channel on the shared ADC1 oneshot unit
     * @param gpio GPIO pin number for ADC input
     * @param atten ADC attenuation setting (default: 12dB for ~0-3.3V range)
     * @param bitwidth ADC resolution (default: 12-bit)
     * @param window_size Moving average window size (>=1)
     * @return true on success, false on failure
     */
    bool init(int gpio, adc_atten_t atten = ADC_ATTEN_DB_12, adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT, size_t window_size = 1);

    /**
     * @brief Perform one ADC conversion and update filtered value
     * @return true on success, false on failure
     */
    bool sampleOnce();

    /**
     * @brief Get the filtered ADC reading in millivolts
     * @return Filtered voltage in mV
     */
    int getFilteredMilliVolts() const
    {
        return filtered_mv_;
    }

    /**
     * @brief Check if ADC sampling is supported on current platform
     * @return true if supported
     */
    static constexpr bool isSupported()
    {
        return true;
    }

   private:
    // Hardware initialization helpers
    bool ensure_unit();
    bool configure_channel(int gpio, adc_atten_t atten, adc_bitwidth_t bitwidth);

    /**
     * @brief Platform-specific GPIO to ADC channel mapping
     * @note Implemented separately in AdcSampler_esp32.cpp and AdcSampler_esp32s3.cpp
     */
    static bool map_gpio_to_channel(int gpio, adc_unit_t& unit, adc_channel_t& channel);

    // Shared ADC1 oneshot handle (single instance for all AdcSampler objects)
    static adc_oneshot_unit_handle_t shared_unit_;

    // Per-instance state
    adc_cali_handle_t cali_handle_{nullptr};
    bool cali_inited_{false};
    adc_channel_t channel_{ADC_CHANNEL_0};
    adc_unit_t unit_{ADC_UNIT_1};
    adc_atten_t atten_{ADC_ATTEN_DB_12};
    adc_bitwidth_t bitwidth_{ADC_BITWIDTH_DEFAULT};

    // Moving average filter state
    std::vector<int> samples_{};
    int sample_sum_{0};
    size_t sample_idx_{0};
    size_t sample_count_{0};
    int filtered_mv_{0};
};

#else
// Stub for unsupported targets to keep interfaces consistent
class AdcSampler
{
   public:
    bool init(int /*gpio*/, int /*atten*/ = 0, int /*bitwidth*/ = 0, size_t /*window_size*/ = 1)
    {
        return false;
    }
    bool sampleOnce()
    {
        return false;
    }
    int getFilteredMilliVolts() const
    {
        return 0;
    }
    static constexpr bool isSupported()
    {
        return false;
    }
};
#endif
