/**
 * @file AdcSampler.cpp
 * @brief BSP Layer - Common ADC sampling implementation
 *
 * This file contains platform-independent ADC sampling logic.
 * Platform-specific implementations are in separate files:
 * - AdcSampler_esp32.cpp   (Tested)
 * - AdcSampler_esp32s3.cpp (Tested)
 * - AdcSampler_esp32s2.cpp (UNTESTED)
 */

#include "AdcSampler.hpp"

#if ADC_SAMPLER_SUPPORTED
#include <esp_log.h>

static const char* TAG = "[AdcSampler]";

// Static member initialization
adc_oneshot_unit_handle_t AdcSampler::shared_unit_ = nullptr;

AdcSampler::~AdcSampler()
{
    if (cali_handle_)
    {
        delete_calibration(cali_handle_);
        cali_handle_ = nullptr;
    }
}

bool AdcSampler::init(int gpio, adc_atten_t atten, adc_bitwidth_t bitwidth, size_t window_size)
{
    // Initialize moving average filter
    if (window_size == 0)
    {
        window_size = 1;
    }
    samples_.assign(window_size, 0);
    sample_sum_ = 0;
    sample_idx_ = 0;
    sample_count_ = 0;

    atten_ = atten;
    bitwidth_ = bitwidth;

    // Map GPIO to ADC channel (platform-specific)
    if (!map_gpio_to_channel(gpio, unit_, channel_))
    {
        ESP_LOGW(TAG, "GPIO %d is not a valid ADC1 pin on this chip", gpio);
        return false;
    }

    // Initialize shared ADC unit
    if (!ensure_unit())
    {
        return false;
    }

    // Configure the ADC channel
    if (!configure_channel(gpio, atten, bitwidth))
    {
        return false;
    }

    // Try calibration (requires eFuse data)
    // Platform-specific: ESP32-S3/S2 use curve-fitting, ESP32 uses line-fitting
    if (create_calibration(&cali_handle_))
    {
        cali_inited_ = true;
        ESP_LOGI(TAG, "ADC calibration initialized");
    }
    else
    {
        cali_inited_ = false;
        ESP_LOGW(TAG, "ADC calibration not available; using raw-to-mV approximation");
    }

    return true;
}

bool AdcSampler::sampleOnce()
{
    if (!shared_unit_)
    {
        return false;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(shared_unit_, channel_, &raw);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        return false;
    }

    int mv = 0;
    if (cali_inited_)
    {
        if (adc_cali_raw_to_voltage(cali_handle_, raw, &mv) != ESP_OK)
        {
            mv = 0;
        }
    }
    else
    {
        // Approximate conversion for 12dB attenuation (~0–3600 mV range)
        // Full-scale raw = (1 << bitwidth_) - 1
        // For 12-bit: max raw = 4095 → ~3600 mV
        int full_scale_mv = 3600;
        int max_raw = (1 << bitwidth_) - 1;
        if (max_raw > 0)
        {
            mv = (raw * full_scale_mv) / max_raw;
        }
        else
        {
            mv = 0;
        }
    }

    // Update moving average filter
    sample_sum_ -= samples_[sample_idx_];
    samples_[sample_idx_] = mv;
    sample_sum_ += mv;
    sample_idx_ = (sample_idx_ + 1) % samples_.size();
    if (sample_count_ < samples_.size())
    {
        sample_count_++;
    }
    filtered_mv_ = sample_sum_ / static_cast<int>(sample_count_ > 0 ? sample_count_ : 1);

    return true;
}

bool AdcSampler::ensure_unit()
{
    if (shared_unit_)
    {
        return true;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &shared_unit_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        shared_unit_ = nullptr;
        return false;
    }
    return true;
}

bool AdcSampler::configure_channel(int gpio, adc_atten_t atten, adc_bitwidth_t bitwidth)
{
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = bitwidth,
    };
    esp_err_t err = adc_oneshot_config_channel(shared_unit_, channel_, &chan_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed (GPIO %d, CH %d): %s", gpio, static_cast<int>(channel_), esp_err_to_name(err));
        return false;
    }
    return true;
}

#endif  // ADC_SAMPLER_SUPPORTED
