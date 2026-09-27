// Only built with CONFIG_CAMERA_POWER_CONTROL (see CMakeLists.txt).
#include "CamLines.hpp"

#include <AdcSampler.hpp>
#include <esp_log.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_rom_sys.h"

static const char* CAM_LINES_TAG = "[CAM_LINES]";

namespace
{
constexpr int kPowerEn = CONFIG_CAMERA_POWER_EN_GPIO;
constexpr int kHwReset = CONFIG_CAMERA_HW_RESET_GPIO;

constexpr int kCameraPins[] = {
    CONFIG_PWDN_GPIO_NUM, CONFIG_RESET_GPIO_NUM, CONFIG_XCLK_GPIO_NUM, CONFIG_SIOD_GPIO_NUM,  CONFIG_SIOC_GPIO_NUM, CONFIG_Y9_GPIO_NUM,
    CONFIG_Y8_GPIO_NUM,   CONFIG_Y7_GPIO_NUM,    CONFIG_Y6_GPIO_NUM,   CONFIG_Y5_GPIO_NUM,    CONFIG_Y4_GPIO_NUM,   CONFIG_Y3_GPIO_NUM,
    CONFIG_Y2_GPIO_NUM,   CONFIG_VSYNC_GPIO_NUM, CONFIG_HREF_GPIO_NUM, CONFIG_PCLK_GPIO_NUM,
};

constexpr bool usedByCamera(int gpio)
{
    for (const int pin : kCameraPins)
    {
        if (pin == gpio)
        {
            return true;
        }
    }
    return false;
}

// Reset node on Rev.5: GPIO -> 1 k -> node with 10 k to 3V3 and 100 nF. Against the
// internal pull-down it stays around 2.2-2.7 V, above the camera's reset threshold,
// so the probe never resets the sensor. An unconnected pad follows both pulls.
// A pull without effect leaves both readings alike: Unknown, never a false Present.
// Thresholds are placeholders until real boards have logged their values.
constexpr int kSettleUs = 3000;  // node tau about 0.8 ms
constexpr int kSamples = 4;
constexpr int kPresentMinMv = 1200;       // pulled down, still held up from outside
constexpr int kMinPullEffectMv = 150;     // and the pull-down visibly moved it
constexpr int kAbsentMaxMv = 400;         // pulled down, nothing holds it up
constexpr int kAbsentMinPullupMv = 2000;  // pulled up, the pad follows
constexpr int kCeExpectedMinMv = 2500;    // Rev.5: 10 k to 3V3, advisory only

int readMilliVolts(AdcSampler& adc)
{
    int sum = 0;
    for (int i = 0; i < kSamples; ++i)
    {
        if (!adc.sampleOnce())
        {
            return -1;
        }
        sum += adc.getFilteredMilliVolts();
    }
    return sum / kSamples;
}

// Input without pulls. gpio_config() also takes the pad out of the RTC mux the
// ADC left it in (same as releasePin() in FanRevision.cpp).
void releaseToInput(int pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

LinePresence classify(int pullup_mv, int pulldown_mv)
{
    if (pullup_mv < 0 || pulldown_mv < 0)
    {
        return LinePresence::Unknown;
    }
    if (pulldown_mv >= kPresentMinMv && pullup_mv - pulldown_mv >= kMinPullEffectMv)
    {
        return LinePresence::Present;
    }
    if (pulldown_mv <= kAbsentMaxMv && pullup_mv >= kAbsentMinPullupMv)
    {
        return LinePresence::Absent;
    }
    return LinePresence::Unknown;
}

// Open drain: low or released, never driven high.
LineOutcome pullLow(int pin)
{
    const auto gpio = static_cast<gpio_num_t>(pin);
    // Level first, so enabling the driver pulls low at once.
    if (gpio_set_level(gpio, 0) != ESP_OK || gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD) != ESP_OK)
    {
        return LineOutcome::Error;
    }
    return LineOutcome::Done;
}

LineOutcome release(int pin)
{
    return gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT) == ESP_OK ? LineOutcome::Done : LineOutcome::Error;
}
}  // namespace

// Build guards: a wrong board config fails here, not on the bench.
static_assert(kPowerEn >= 0 && kHwReset >= 0, "camera lines not configured");
static_assert(kPowerEn != kHwReset, "CAM_CE and CAM_RESET on the same GPIO");
static_assert(!usedByCamera(kPowerEn) && !usedByCamera(kHwReset), "camera line collides with a camera pin");
// The camera driver would drive its own reset pin push-pull.
static_assert(CONFIG_RESET_GPIO_NUM < 0, "driver reset pin must stay unused, CAM_RESET is open drain");
#if CONFIG_LED_DEBUG_ENABLE
static_assert(CONFIG_LED_DEBUG_GPIO != kPowerEn && CONFIG_LED_DEBUG_GPIO != kHwReset, "debug LED collides with a camera line");
#endif
#if CONFIG_LED_EXTERNAL_CONTROL
static_assert(CONFIG_LED_EXTERNAL_GPIO != kPowerEn && CONFIG_LED_EXTERNAL_GPIO != kHwReset, "IR LED collides with a camera line");
#endif
#if CONFIG_FAN_PWM_ENABLE
static_assert(CONFIG_FAN_PWM_GPIO != kPowerEn && CONFIG_FAN_PWM_GPIO != kHwReset, "fan PWM collides with a camera line");
#endif
#if CONFIG_MONITORING_LED_CURRENT
static_assert(CONFIG_MONITORING_LED_ADC_GPIO != kPowerEn && CONFIG_MONITORING_LED_ADC_GPIO != kHwReset,
              "LED current sense collides with a camera line");
#endif
#if CONFIG_MONITORING_BATTERY_ENABLE
static_assert(CONFIG_MONITORING_BATTERY_ADC_GPIO != kPowerEn && CONFIG_MONITORING_BATTERY_ADC_GPIO != kHwReset,
              "battery sense collides with a camera line");
#endif

const char* linePresenceName(const LinePresence presence)
{
    switch (presence)
    {
        case LinePresence::Present:
            return "present";
        case LinePresence::Absent:
            return "absent";
        default:
            return "unknown";
    }
}

const char* lineOutcomeName(const LineOutcome outcome)
{
    switch (outcome)
    {
        case LineOutcome::Done:
            return "done";
        case LineOutcome::NotAvailable:
            return "not_available";
        default:
            return "error";
    }
}

const LineProbe& CamLines::probe()
{
    LineProbe result{};
    const auto reset_pin = static_cast<gpio_num_t>(kHwReset);
    {
        AdcSampler adc;
        if (adc.init(kHwReset, ADC_ATTEN_DB_12))
        {
            // The ADC cleared the RTC pulls. Clear the digital ones too, so only one set is in play.
            gpio_pullup_dis(reset_pin);
            gpio_pulldown_dis(reset_pin);
            // Pull-up first: on Rev.5 the node is already high, nothing moves.
            rtc_gpio_pullup_en(reset_pin);
            esp_rom_delay_us(kSettleUs);
            result.reset_pullup_mv = readMilliVolts(adc);
            rtc_gpio_pullup_dis(reset_pin);

            rtc_gpio_pulldown_en(reset_pin);
            esp_rom_delay_us(kSettleUs);
            result.reset_pulldown_mv = readMilliVolts(adc);
            rtc_gpio_pulldown_dis(reset_pin);
        }
        releaseToInput(kHwReset);
    }
    {
        // Never pull CE down: the LDO enable threshold is unknown.
        AdcSampler adc;
        if (adc.init(kPowerEn, ADC_ATTEN_DB_12))
        {
            gpio_pullup_dis(static_cast<gpio_num_t>(kPowerEn));
            gpio_pulldown_dis(static_cast<gpio_num_t>(kPowerEn));
            result.ce_mv = readMilliVolts(adc);
        }
        releaseToInput(kPowerEn);
    }

    result.presence = classify(result.reset_pullup_mv, result.reset_pulldown_mv);
    probe_ = result;
    power_off_ = false;
    reset_held_ = false;

    if (result.presence == LinePresence::Unknown)
    {
        ESP_LOGW(CAM_LINES_TAG, "CE/RESET lines: unknown (reset node %d mV pulled up, %d mV pulled down; CE %d mV)", result.reset_pullup_mv,
                 result.reset_pulldown_mv, result.ce_mv);
    }
    else
    {
        ESP_LOGI(CAM_LINES_TAG, "CE/RESET lines: %s (reset node %d mV pulled up, %d mV pulled down; CE %d mV)", linePresenceName(result.presence),
                 result.reset_pullup_mv, result.reset_pulldown_mv, result.ce_mv);
    }
    if (result.presence == LinePresence::Present && result.ce_mv >= 0 && result.ce_mv < kCeExpectedMinMv)
    {
        ESP_LOGW(CAM_LINES_TAG, "CE reads %d mV although the lines are present; camera LDOs may be off", result.ce_mv);
    }
    return probe_;
}

LineOutcome CamLines::powerOff(const bool force)
{
    if (!available() && !force)
    {
        return LineOutcome::NotAvailable;
    }
    const LineOutcome outcome = pullLow(kPowerEn);
    power_off_ = outcome == LineOutcome::Done;
    return outcome;
}

LineOutcome CamLines::powerOn(const bool force)
{
    const LineOutcome outcome = release(kPowerEn);
    if (outcome != LineOutcome::Done)
    {
        return outcome;
    }
    power_off_ = false;
    return available() || force ? LineOutcome::Done : LineOutcome::NotAvailable;
}

LineOutcome CamLines::holdReset(const bool force)
{
    if (!available() && !force)
    {
        return LineOutcome::NotAvailable;
    }
    const LineOutcome outcome = pullLow(kHwReset);
    reset_held_ = outcome == LineOutcome::Done;
    return outcome;
}

LineOutcome CamLines::releaseReset(const bool force)
{
    const LineOutcome outcome = release(kHwReset);
    if (outcome != LineOutcome::Done)
    {
        return outcome;
    }
    reset_held_ = false;
    return available() || force ? LineOutcome::Done : LineOutcome::NotAvailable;
}
