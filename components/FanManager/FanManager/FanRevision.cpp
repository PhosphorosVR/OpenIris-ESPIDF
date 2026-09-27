#include "FanRevision.hpp"

#include <esp_log.h>

static const char* FAN_REV_TAG = "[FAN_REVISION]";

const char* fanRevisionName(FanRevision revision)
{
    switch (revision)
    {
        case FanRevision::Legacy:
            return "legacy";
        case FanRevision::Rev5:
            return "rev5";
        default:
            return "unknown";
    }
}

#ifdef CONFIG_FAN_REV_DETECT

#include <AdcSampler.hpp>
#include "driver/rtc_io.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
// 5 ms is five time constants of the Rev.5 charge path (1 k x 1 uF) and about
// fifty of the legacy gate. Kept as short as the measurement allows, because on
// Rev <= 4.5 this window drives the MOSFET fully on.
constexpr int kChargeMs = 5;

// Sample A decides. Its window is enormous - even a 15 ms delay still reads
// above 2.1 V on Rev.5 - so the exact instant matters far less than the fact
// that we know what it was.
constexpr int kSampleAUs = 2500;
constexpr int kSampleBUs = 30000;

// Model: U(t) = 0.21 V + 3.04 V * e^(-t/tau).
//   Rev.5   tau 35..80 ms  ->  A = 3040..3160 mV, B = 1050..2440 mV
//   Legacy  tau 70..150 us ->  A and B below 1 mV
// The threshold sits in the middle of the gap A opens up.
constexpr int kThresholdRev5Mv = 1500;
constexpr int kThresholdLegacyMv = 400;

// A node stuck at 3V3 (cap open, short to rail) reads A high AND B high with no
// decay at all. Real Rev.5 hardware drops at least 720 mV between the samples,
// ADC noise is around 15 mV, so 200 mV separates the two cleanly.
constexpr int kMinDecayMv = 200;

// Purely advisory: B outside this band means the decay does not match the model,
// which is worth a log line but must never change the verdict.
constexpr int kPlausibleBLowMv = 1000;
constexpr int kPlausibleBHighMv = 2600;

// The scheduler is NOT suspended around the samples: the ADC calibration and
// gpio_config() both log, and logging takes a mutex, which asserts with the
// scheduler suspended. The window is timed instead. Past this point preemption
// stretched it; a healthy Rev.5 node would still read above 2.1 V, but the
// measurement is repeated rather than trusted. The digital test has less room -
// it crosses V_IH after about 10 ms at worst-case tau - so it matters there.
constexpr int kStretchedAUs = 8000;
constexpr int kMaxAttempts = 3;

/**
 * @brief Hand the pin back as a plain digital pad with nothing driving it.
 *
 * gpio_config() calls rtc_gpio_deinit() internally, which is what takes the pad
 * out of the analog mode the ADC left it in - without that, LEDC would later
 * configure the matrix but the signal would never reach the pin.
 *
 * gpio_reset_pin() would deinit the RTC mux too, but it enables the pull-up on
 * the way out. On Rev <= 4.5 the internal pull-up against the 100 k gate
 * pulldown lands around 2.3 V, above the MOSFET threshold.
 */
void releasePin(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

FanRevision classify(int a_mv, int b_mv)
{
    if (a_mv > kThresholdRev5Mv && b_mv < a_mv - kMinDecayMv)
    {
        if (b_mv < kPlausibleBLowMv || b_mv > kPlausibleBHighMv)
        {
            ESP_LOGW(FAN_REV_TAG, "sample B %d mV outside the expected %d..%d band; decay looks off but A is unambiguous", b_mv, kPlausibleBLowMv,
                     kPlausibleBHighMv);
        }
        return FanRevision::Rev5;
    }

    if (a_mv < kThresholdLegacyMv && b_mv < kThresholdLegacyMv)
    {
        return FanRevision::Legacy;
    }

    return FanRevision::Unknown;
}

/**
 * @brief Charge and measure without ever leaving the RTC domain.
 *
 * The ADC puts the pad into RTC mode. Driving the charge with plain
 * gpio_config() would call rtc_gpio_deinit() and disarm the ADC, so the charge
 * runs through the RTC output driver instead and releasing is a single
 * direction change - which also makes the zero point of the decay exact.
 */
bool measureWithAdc(gpio_num_t pin, FanRevisionResult& out)
{
    AdcSampler adc;
    if (!adc.init(static_cast<int>(pin), ADC_ATTEN_DB_12))
    {
        ESP_LOGW(FAN_REV_TAG, "ADC init failed on GPIO %d, falling back to the digital test", static_cast<int>(pin));
        return false;
    }

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        // Level before direction: the RTC output register may hold anything.
        rtc_gpio_set_level(pin, 1);
        rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
        vTaskDelay(pdMS_TO_TICKS(kChargeMs));

        rtc_gpio_set_direction(pin, RTC_GPIO_MODE_DISABLED);
        const int64_t released_us = esp_timer_get_time();
        esp_rom_delay_us(kSampleAUs);
        const bool a_ok = adc.sampleOnce();
        const int a_mv = adc.getFilteredMilliVolts();
        out.release_to_a_us = static_cast<int>(esp_timer_get_time() - released_us);

        if (out.release_to_a_us > kStretchedAUs && attempt < kMaxAttempts)
        {
            ESP_LOGW(FAN_REV_TAG, "sample A came %d us after release (attempt %d), measuring again", out.release_to_a_us, attempt);
            continue;
        }

        // Sample B may drift - it only has to show that the node is falling.
        const int64_t remaining_us = kSampleBUs - (esp_timer_get_time() - released_us);
        if (remaining_us > 0)
        {
            vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000));
        }
        const bool b_ok = adc.sampleOnce();
        const int b_mv = adc.getFilteredMilliVolts();

        releasePin(pin);

        if (!a_ok || !b_ok)
        {
            ESP_LOGW(FAN_REV_TAG, "ADC read failed, falling back to the digital test");
            return false;
        }

        out.used_adc = true;
        out.sample_a_mv = a_mv;
        out.sample_b_mv = b_mv;
        out.revision = classify(a_mv, b_mv);
        return true;
    }

    releasePin(pin);
    return false;
}

/**
 * @brief Level-only fallback for when the ADC is unavailable.
 *
 * Always produces an answer and therefore has no way to report a broken node -
 * which is exactly why it is the fallback and not the primary path.
 */
void measureDigital(gpio_num_t pin, FanRevisionResult& out)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    int level = 0;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        gpio_set_level(pin, 1);
        gpio_config(&out_cfg);
        gpio_set_level(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(kChargeMs));

        // gpio_set_direction() rather than gpio_config(): the latter logs, and
        // a log line here would sit between release and sample. Pulls stay off
        // from the output configuration above.
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        const int64_t released_us = esp_timer_get_time();
        esp_rom_delay_us(kSampleAUs);
        level = gpio_get_level(pin);
        out.release_to_a_us = static_cast<int>(esp_timer_get_time() - released_us);

        if (out.release_to_a_us <= kStretchedAUs || attempt == kMaxAttempts)
        {
            break;
        }
        ESP_LOGW(FAN_REV_TAG, "digital sample came %d us after release (attempt %d), measuring again", out.release_to_a_us, attempt);
    }

    releasePin(pin);

    out.used_adc = false;
    out.revision = level ? FanRevision::Rev5 : FanRevision::Legacy;
    ESP_LOGW(FAN_REV_TAG, "digital fallback read %s -> %s (no way to report a broken node this way)", level ? "high" : "low",
             fanRevisionName(out.revision));
}
}  // namespace

FanRevisionResult detectFanRevision(gpio_num_t pin)
{
    FanRevisionResult result;
    result.measured = true;

    if (!measureWithAdc(pin, result))
    {
        measureDigital(pin, result);
    }

    if (result.release_to_a_us > kStretchedAUs)
    {
        ESP_LOGW(FAN_REV_TAG, "sample A was taken %d us after release instead of %d us on every attempt", result.release_to_a_us, kSampleAUs);
    }

    ESP_LOGI(FAN_REV_TAG, "fan node decay: A=%d mV @%d us, B=%d mV @%d us -> %s (%s)", result.sample_a_mv, result.release_to_a_us, result.sample_b_mv,
             kSampleBUs, fanRevisionName(result.revision), result.used_adc ? "adc" : "digital");

    return result;
}

#else  // !CONFIG_FAN_REV_DETECT

FanRevisionResult detectFanRevision(gpio_num_t pin)
{
    (void)pin;
    return FanRevisionResult{};
}

#endif  // CONFIG_FAN_REV_DETECT
