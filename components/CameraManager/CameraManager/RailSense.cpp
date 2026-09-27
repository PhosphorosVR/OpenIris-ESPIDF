// Only built with CONFIG_CAMERA_RAIL_SENSE (see CMakeLists.txt).
#include "RailSense.hpp"

#include <esp_log.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_rom_sys.h"

static const char* RAIL_SENSE_TAG = "[RAIL_SENSE]";

namespace
{
constexpr int kPadGpio[RailSense::kPads] = {CONFIG_CAMERA_RAIL_SENSE_GPIO_A, CONFIG_CAMERA_RAIL_SENSE_GPIO_B};

constexpr int kDataPins[] = {
    CONFIG_Y2_GPIO_NUM, CONFIG_Y3_GPIO_NUM, CONFIG_Y4_GPIO_NUM, CONFIG_Y5_GPIO_NUM,
    CONFIG_Y6_GPIO_NUM, CONFIG_Y7_GPIO_NUM, CONFIG_Y8_GPIO_NUM, CONFIG_Y9_GPIO_NUM,
};

constexpr bool isDataPin(int gpio)
{
    for (const int pin : kDataPins)
    {
        if (pin == gpio)
        {
            return true;
        }
    }
    return false;
}

// The pad alone settles within microseconds against the pull-up; the rail behind the
// diode barely moves from ~60 uA in this time.
constexpr int kSettleUs = 200;
// Same approximation as AdcSampler when no eFuse calibration exists.
constexpr int kUncalibratedFullScaleMv = 3600;

void toDigitalInput(int pin)
{
    // gpio_config() also takes the pad out of the RTC mux; without that the DVP input stays dead.
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}
}  // namespace

static_assert(kPadGpio[0] >= 0 && kPadGpio[1] >= 0, "rail sense pads not configured");
static_assert(kPadGpio[0] != kPadGpio[1], "rail sense pads must differ");
static_assert(isDataPin(kPadGpio[0]) && isDataPin(kPadGpio[1]), "rail sense pads must be DVP data pins");
#if CONFIG_IDF_TARGET_ESP32S3
// ADC2 is GPIO11..20 on the S3 (checked again at runtime through the ADC driver).
static_assert(kPadGpio[0] >= 11 && kPadGpio[0] <= 20 && kPadGpio[1] >= 11 && kPadGpio[1] <= 20, "rail sense pads must be ADC2 pins");
#endif
#if CONFIG_RTC_CLK_SRC_EXT_CRYS || CONFIG_RTC_CLK_SRC_EXT_OSC
// GPIO15/16 carry the 32 kHz crystal on the S3.
static_assert(kPadGpio[0] != 15 && kPadGpio[0] != 16 && kPadGpio[1] != 15 && kPadGpio[1] != 16, "rail sense pad is a 32 kHz crystal pin");
#endif

RailSense::~RailSense()
{
    if (active_)
    {
        end();
    }
}

bool RailSense::begin()
{
    if (active_)
    {
        return true;
    }
    // From here on end() must run, even if a step fails.
    active_ = true;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_2,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &unit_); err != ESP_OK)
    {
        ESP_LOGE(RAIL_SENSE_TAG, "ADC2 unit unavailable: %s", esp_err_to_name(err));
        unit_ = nullptr;
        return false;
    }

    for (int i = 0; i < kPads; ++i)
    {
        adc_unit_t unit;
        if (adc_oneshot_io_to_channel(kPadGpio[i], &unit, &channel_[i]) != ESP_OK || unit != ADC_UNIT_2)
        {
            ESP_LOGE(RAIL_SENSE_TAG, "GPIO %d is not an ADC2 pad", kPadGpio[i]);
            return false;
        }
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (esp_err_t err = adc_oneshot_config_channel(unit_, channel_[i], &chan_cfg); err != ESP_OK)
        {
            ESP_LOGE(RAIL_SENSE_TAG, "ADC2 channel for GPIO %d: %s", kPadGpio[i], esp_err_to_name(err));
            return false;
        }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = ADC_UNIT_2,
            .chan = channel_[i],
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_[i]) != ESP_OK)
        {
            cali_[i] = nullptr;
        }
#endif

        // The ADC cleared the RTC pulls. Clear the digital ones too, so only one set is in play.
        const auto gpio = static_cast<gpio_num_t>(kPadGpio[i]);
        gpio_pullup_dis(gpio);
        gpio_pulldown_dis(gpio);
        rtc_gpio_pulldown_en(gpio);
    }
    ready_ = true;
    return true;
}

bool RailSense::measure(int (&mv)[kPads], const int samples)
{
    for (int& value : mv)
    {
        value = -1;
    }
    if (!ready_ || samples < 1)
    {
        return false;
    }

    for (const int pin : kPadGpio)
    {
        rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
        rtc_gpio_pullup_en(static_cast<gpio_num_t>(pin));
    }
    esp_rom_delay_us(kSettleUs);

    bool ok = true;
    for (int i = 0; i < kPads && ok; ++i)
    {
        int sum = 0;
        for (int s = 0; s < samples; ++s)
        {
            int raw = 0;
            if (adc_oneshot_read(unit_, channel_[i], &raw) != ESP_OK)
            {
                ok = false;
                break;
            }
            int value = raw * kUncalibratedFullScaleMv / 4095;
            if (cali_[i] && adc_cali_raw_to_voltage(cali_[i], raw, &value) != ESP_OK)
            {
                value = raw * kUncalibratedFullScaleMv / 4095;
            }
            sum += value;
        }
        if (ok)
        {
            mv[i] = sum / samples;
        }
    }

    for (const int pin : kPadGpio)
    {
        rtc_gpio_pullup_dis(static_cast<gpio_num_t>(pin));
        rtc_gpio_pulldown_en(static_cast<gpio_num_t>(pin));
    }
    return ok;
}

void RailSense::end()
{
    for (auto& cali : cali_)
    {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        if (cali)
        {
            adc_cali_delete_scheme_curve_fitting(cali);
        }
#endif
        cali = nullptr;
    }
    if (unit_)
    {
        adc_oneshot_del_unit(unit_);
        unit_ = nullptr;
    }
    for (const int pin : kPadGpio)
    {
        rtc_gpio_pullup_dis(static_cast<gpio_num_t>(pin));
        rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
        toDigitalInput(pin);
    }
    ready_ = false;
    active_ = false;
}
