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
#endif

FanManager::FanManager(gpio_num_t fan_pin, std::shared_ptr<ProjectConfig> deviceConfig) : fan_pin(fan_pin), deviceConfig(std::move(deviceConfig)) {}

void FanManager::setup()
{
#ifdef CONFIG_FAN_PWM_ENABLE
    const int freq = CONFIG_FAN_PWM_FREQ;
    const auto resolution = LEDC_TIMER_8_BIT;
    const auto cfg = this->deviceConfig->getDeviceConfig();
    const uint8_t clampedPercent = clampFanDuty(static_cast<uint8_t>(cfg.fan_pwm_duty_cycle));
    const uint32_t dutyCycle = (clampedPercent * 255) / 100;

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
    const uint32_t dutyCycle = (static_cast<uint32_t>(clampedPercent) * 255) / 100;
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
