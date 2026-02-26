#include "FanManager.hpp"

#include <algorithm>

static const char* FAN_MANAGER_TAG = "[FAN_MANAGER]";

#ifdef CONFIG_FAN_PWM_ENABLE
constexpr ledc_timer_t FAN_PWM_TIMER = LEDC_TIMER_2;
constexpr ledc_channel_t FAN_PWM_CHANNEL = LEDC_CHANNEL_2;

static uint8_t clampFanDuty(uint8_t duty)
{
    const int lo = std::min(CONFIG_FAN_PWM_DUTY_MIN, CONFIG_FAN_PWM_DUTY_MAX);
    const int hi = std::max(CONFIG_FAN_PWM_DUTY_MIN, CONFIG_FAN_PWM_DUTY_MAX);
    return static_cast<uint8_t>(std::clamp<int>(duty, lo, hi));
}

/**
 * @brief Linearization LUT: user percent (0-100) -> raw 10-bit LEDC duty (0-1023).
 *
 * Compensates for the nonlinear PWM-to-motor-voltage characteristic of low-side
 * PWM fan drives.  Values are pre-computed from measured voltage data so that
 * equal user-percent steps produce approximately equal voltage steps.
 *
 * 10-bit resolution provides ~4x finer granularity than 8-bit, especially in
 * the critical low-PWM range where voltage changes most rapidly.
 *
 * Recalibrate by re-running the companion Python script with new measurements.
 */
// clang-format off
static constexpr uint16_t kFanLinearizationLut[101] = {
    /*   0% */    0,   42,   44,   45,   46,   47,   49,   50,   51,   52,   53,
    /*  11% */   54,   54,   55,   56,   57,   57,   58,   59,   60,   61,   61,
    /*  22% */   63,   64,   66,   67,   69,   70,   72,   73,   75,   77,   78,
    /*  33% */   80,   81,   85,   89,   92,   94,   96,   98,  100,  102,  104,
    /*  44% */  106,  108,  110,  112,  115,  119,  123,  125,  128,  130,  132,
    /*  55% */  141,  146,  150,  154,  161,  167,  174,  180,  187,  193,  199,
    /*  66% */  207,  216,  226,  236,  246,  255,  267,  279,  292,  304,  321,
    /*  77% */  341,  360,  380,  399,  418,  438,  457,  477,  496,  517,  545,
    /*  88% */  573,  601,  628,  656,  684,  712,  754,  799,  844,  888,  933,
    /*  99% */  978, 1023
};
// clang-format on

static uint32_t linearizeFanDuty(uint8_t userPercent)
{
    const uint8_t idx = std::min<uint8_t>(userPercent, 100);
    return static_cast<uint32_t>(kFanLinearizationLut[idx]);
}
#endif

FanManager::FanManager(gpio_num_t fan_pin, std::shared_ptr<ProjectConfig> deviceConfig) : fan_pin(fan_pin), deviceConfig(std::move(deviceConfig)) {}

void FanManager::setup()
{
#ifdef CONFIG_FAN_PWM_ENABLE
    const int freq = CONFIG_FAN_PWM_FREQ;
    const auto resolution = LEDC_TIMER_10_BIT;
    const auto cfg = this->deviceConfig->getDeviceConfig();
    const uint8_t clampedPercent = clampFanDuty(static_cast<uint8_t>(cfg.fan_pwm_duty_cycle));
    const uint32_t dutyCycle = linearizeFanDuty(clampedPercent);

    ESP_LOGI(FAN_MANAGER_TAG, "Setting up fan PWM on GPIO %d, freq=%d Hz, duty=%lu (%%=%u)", static_cast<int>(fan_pin), freq, dutyCycle,
             clampedPercent);

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = resolution, .timer_num = FAN_PWM_TIMER, .freq_hz = freq, .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {.gpio_num = this->fan_pin,
                                         .speed_mode = LEDC_LOW_SPEED_MODE,
                                         .channel = FAN_PWM_CHANNEL,
                                         .intr_type = LEDC_INTR_DISABLE,
                                         .timer_sel = FAN_PWM_TIMER,
                                         .duty = dutyCycle,
                                         .hpoint = 0};

    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    initialized = true;
#else
    ESP_LOGW(FAN_MANAGER_TAG, "CONFIG_FAN_PWM_ENABLE not set; skipping fan setup");
#endif
}

void FanManager::setFanDutyCycle(uint8_t dutyPercent)
{
#ifdef CONFIG_FAN_PWM_ENABLE
    if (!initialized)
    {
        ESP_LOGW(FAN_MANAGER_TAG, "Fan PWM not initialized; ignoring duty update");
        return;
    }

    const uint8_t clampedPercent = clampFanDuty(dutyPercent);
    const uint32_t dutyCycle = linearizeFanDuty(clampedPercent);
    ESP_LOGI(FAN_MANAGER_TAG, "Updating fan duty to %u%% (raw %lu)", clampedPercent, dutyCycle);
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, dutyCycle));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL));
#else
    (void)dutyPercent;
    ESP_LOGW(FAN_MANAGER_TAG, "CONFIG_FAN_PWM_ENABLE not set; ignoring duty update");
#endif
}

uint8_t FanManager::getFanDutyCycle() const
{
    return deviceConfig ? deviceConfig->getDeviceConfig().fan_pwm_duty_cycle : 0;
}
