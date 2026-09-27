#include "FanManager.hpp"

#include <algorithm>
#include "FanCurve.hpp"

static const char* FAN_MANAGER_TAG = "[FAN_MANAGER]";

#ifdef CONFIG_FAN_PWM_ENABLE
constexpr ledc_timer_t FAN_PWM_TIMER = LEDC_TIMER_2;
constexpr ledc_channel_t FAN_PWM_CHANNEL = LEDC_CHANNEL_2;

// Only defined when revision detection is enabled; keep the manager compilable
// in every Kconfig combination.
#ifndef CONFIG_FAN_KICKSTART_PERCENT
#define CONFIG_FAN_KICKSTART_PERCENT 100
#endif
#ifndef CONFIG_FAN_KICKSTART_MS
#define CONFIG_FAN_KICKSTART_MS 400
#endif

// Values a service command may write to "board_rev" to pin the curve down.
// Anything else, 0 included, means "measure every boot" - which is the default
// and the only behaviour a device that was never provisioned can rely on.
constexpr int kOverrideLegacy = 45;
constexpr int kOverrideRev5 = 50;
#endif

FanManager::FanManager(gpio_num_t fan_pin, std::shared_ptr<ProjectConfig> deviceConfig) : fan_pin(fan_pin), deviceConfig(std::move(deviceConfig)) {}

void FanManager::setup()
{
#ifdef CONFIG_FAN_PWM_ENABLE
    // Measure before LEDC claims the pin. Detection leaves it disabled with both
    // pulls off and the RTC mux deinitialised, ready to hand over.
    const int override_value = this->deviceConfig->getDeviceConfig().board_revision_override;
    if (override_value == kOverrideLegacy || override_value == kOverrideRev5)
    {
        revision_from_override = true;
        detection = detectFanRevision(this->fan_pin);
        const FanRevision forced = (override_value == kOverrideRev5) ? FanRevision::Rev5 : FanRevision::Legacy;
        if (detection.measured && detection.revision != forced)
        {
            ESP_LOGW(FAN_MANAGER_TAG, "NVS override says %s but the pin measured %s; honouring the override", fanRevisionName(forced),
                     fanRevisionName(detection.revision));
        }
        detection.revision = forced;
    }
    else
    {
        detection = detectFanRevision(this->fan_pin);
    }

    // Anything that is not a confident Rev.5 runs the legacy curve. On an old
    // board that is bit-for-bit today's behaviour, which is the only fallback
    // that cannot regress hardware already in the field.
    if (detection.revision == FanRevision::Rev5)
    {
        curve = &fanCurveRev5Linear();
    }
    else
    {
        curve = &fanCurveLegacyLut();
        fallback_active = detection.measured && detection.revision == FanRevision::Unknown;
    }

    if (fallback_active)
    {
        ESP_LOGE(FAN_MANAGER_TAG, "Fan drive revision could not be identified (A=%d mV, B=%d mV). Falling back to the legacy curve.", detection.sample_a_mv,
                 detection.sample_b_mv);
        ESP_LOGE(FAN_MANAGER_TAG, "On a Rev.5 board this inverts the fan control. Check the board and read get_board_revision.");
    }

    const auto cfg = this->deviceConfig->getDeviceConfig();
    const uint8_t targetPercent = clampToAllowed(cfg.fan_pwm_duty_cycle);

    // On Rev.5 the pin is already at full output when we get here - the RC node
    // comes out of reset discharged - so starting at the kickstart level costs
    // nothing and guarantees the fan is turning before we ask for a low setting.
    const int kickPercentCfg = cfg.fan_kickstart_percent >= 0 ? cfg.fan_kickstart_percent : CONFIG_FAN_KICKSTART_PERCENT;
    const int kickMs = cfg.fan_kickstart_ms >= 0 ? cfg.fan_kickstart_ms : CONFIG_FAN_KICKSTART_MS;
    const bool wantsKick = curve->usesKickstart() && kickMs > 0 && kickPercentCfg > targetPercent;

    const uint8_t startPercent = wantsKick ? static_cast<uint8_t>(std::clamp(kickPercentCfg, 0, 100)) : targetPercent;
    const uint32_t dutyCycle = curve->dutyForPercent(startPercent);

    ESP_LOGI(FAN_MANAGER_TAG, "Fan on GPIO %d: curve=%s revision=%s freq=%lu Hz duty=%lu (%u%%)", static_cast<int>(fan_pin), curve->name(),
             fanRevisionName(detection.revision), static_cast<unsigned long>(curve->frequencyHz()), static_cast<unsigned long>(dutyCycle), startPercent);

    ledc_timer_config_t timer_cfg = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                     .duty_resolution = curve->resolution(),
                                     .timer_num = FAN_PWM_TIMER,
                                     .freq_hz = curve->frequencyHz(),
                                     // APB, always. The low-speed timers share one clock mux on the
                                     // ESP32-S3; if AUTO ever picked XTAL here the camera would lose
                                     // its XCLK timer with "timer clock conflict".
                                     .clk_cfg = LEDC_AUTO_CLK};

    ledc_channel_config_t channel_cfg = {.gpio_num = this->fan_pin,
                                         .speed_mode = LEDC_LOW_SPEED_MODE,
                                         .channel = FAN_PWM_CHANNEL,
                                         .intr_type = LEDC_INTR_DISABLE,
                                         .timer_sel = FAN_PWM_TIMER,
                                         .duty = dutyCycle,
                                         .hpoint = 0};

    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(FAN_MANAGER_TAG, "Fan PWM timer config failed: %s", esp_err_to_name(err));
        return;
    }

    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(FAN_MANAGER_TAG, "Fan PWM channel config failed: %s", esp_err_to_name(err));
        return;
    }

    initialized = true;

    if (wantsKick)
    {
        kick_target_percent = targetPercent;
        const esp_timer_create_args_t args = {
            .callback = &FanManager::kickTimerCallback, .arg = this, .dispatch_method = ESP_TIMER_TASK, .name = "fan_kick", .skip_unhandled_events = true};
        if (esp_timer_create(&args, &kick_timer) == ESP_OK)
        {
            kick_pending.store(true);
            ESP_LOGI(FAN_MANAGER_TAG, "Kickstart at %u%% for %d ms, then %u%%", startPercent, kickMs, targetPercent);
            esp_timer_start_once(kick_timer, static_cast<uint64_t>(kickMs) * 1000ULL);
        }
        else
        {
            ESP_LOGW(FAN_MANAGER_TAG, "Could not create the kickstart timer; applying the target directly");
            applyPercent(targetPercent);
        }
    }
#else
    ESP_LOGW(FAN_MANAGER_TAG, "CONFIG_FAN_PWM_ENABLE not set; skipping fan setup");
#endif
}

void FanManager::kickTimerCallback(void* arg)
{
    static_cast<FanManager*>(arg)->finishKickstart();
}

void FanManager::finishKickstart()
{
#ifdef CONFIG_FAN_PWM_ENABLE
    // A command that arrived during the kickstart window clears the flag and
    // wins; this callback then has nothing left to do.
    if (!kick_pending.exchange(false))
    {
        return;
    }
    ESP_LOGI(FAN_MANAGER_TAG, "Kickstart done, settling to %u%%", kick_target_percent);
    applyPercent(kick_target_percent);
#endif
}

void FanManager::applyPercent(uint8_t percent)
{
#ifdef CONFIG_FAN_PWM_ENABLE
    if (!initialized || curve == nullptr)
    {
        return;
    }
    const uint32_t dutyCycle = curve->dutyForPercent(percent);
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, dutyCycle));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL));
#else
    (void)percent;
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

    kick_pending.store(false);

    const uint8_t clampedPercent = clampToAllowed(dutyPercent);

    if (fallback_active)
    {
        ESP_LOGE(FAN_MANAGER_TAG, "Fan revision unknown; %u%% is being applied on the legacy curve, which is inverted on Rev.5 hardware", clampedPercent);
    }

    ESP_LOGI(FAN_MANAGER_TAG, "Updating fan duty to %u%% (raw %lu, curve %s)", clampedPercent, static_cast<unsigned long>(curve->dutyForPercent(clampedPercent)),
             curve->name());
    applyPercent(clampedPercent);
#else
    (void)dutyPercent;
    ESP_LOGW(FAN_MANAGER_TAG, "CONFIG_FAN_PWM_ENABLE not set; ignoring duty update");
#endif
}

uint8_t FanManager::getFanDutyCycle() const
{
    return deviceConfig ? static_cast<uint8_t>(deviceConfig->getDeviceConfig().fan_pwm_duty_cycle) : 0;
}

uint8_t FanManager::minAllowedPercent() const
{
#ifdef CONFIG_FAN_PWM_ENABLE
    // An operator override replaces the curve default; without one the curve
    // decides, which keeps CONFIG_FAN_PWM_DUTY_MIN in charge on legacy boards.
    if (deviceConfig)
    {
        const int override_value = deviceConfig->getDeviceConfig().fan_min_percent;
        if (override_value >= 0)
        {
            return static_cast<uint8_t>(std::clamp(override_value, 0, 100));
        }
    }
    if (curve != nullptr)
    {
        return curve->defaultMinPercent();
    }
    return static_cast<uint8_t>(std::clamp(CONFIG_FAN_PWM_DUTY_MIN, 0, 100));
#else
    return 0;
#endif
}

uint8_t FanManager::maxAllowedPercent() const
{
#ifdef CONFIG_FAN_PWM_ENABLE
    return static_cast<uint8_t>(std::clamp(CONFIG_FAN_PWM_DUTY_MAX, 0, 100));
#else
    return 100;
#endif
}

// std::minmax would bind its result to the temporaries returned by the two
// accessors, so take the values first.
uint8_t FanManager::clampToAllowed(int percent) const
{
    const int a = minAllowedPercent();
    const int b = maxAllowedPercent();
    return static_cast<uint8_t>(std::clamp(percent, std::min(a, b), std::max(a, b)));
}

bool FanManager::isPercentAllowed(int percent) const
{
    const int a = minAllowedPercent();
    const int b = maxAllowedPercent();
    return percent >= std::min(a, b) && percent <= std::max(a, b);
}

uint32_t FanManager::maxRawDuty() const
{
#ifdef CONFIG_FAN_PWM_ENABLE
    return curve != nullptr ? curve->fullDuty() : 0;
#else
    return 0;
#endif
}

bool FanManager::setRawDuty(uint32_t raw)
{
#ifdef CONFIG_FAN_PWM_ENABLE
    if (!initialized || curve == nullptr)
    {
        return false;
    }
    kick_pending.store(false);
    const uint32_t clamped = std::min(raw, curve->fullDuty());
    ESP_LOGW(FAN_MANAGER_TAG, "Bench override: raw duty %lu of %lu, curve bypassed", static_cast<unsigned long>(clamped),
             static_cast<unsigned long>(curve->fullDuty()));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, clamped));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL));
    return true;
#else
    (void)raw;
    return false;
#endif
}

FanStatus FanManager::status() const
{
    FanStatus s;
    s.duty_percent = getFanDutyCycle();
    s.min_percent = minAllowedPercent();
    s.max_percent = maxAllowedPercent();
    s.initialized = initialized;
    s.fallback_active = fallback_active;
    s.revision = fanRevisionName(detection.revision);
    s.sample_a_mv = detection.sample_a_mv;
    s.sample_b_mv = detection.sample_b_mv;

    if (revision_from_override)
    {
        s.revision_source = "override";
    }
    else if (detection.measured)
    {
        s.revision_source = "measured";
    }

#ifdef CONFIG_FAN_PWM_ENABLE
    if (curve != nullptr)
    {
        s.curve = curve->name();
        s.can_turn_off = curve->canTurnOff();
    }
#endif
    return s;
}
