#pragma once
#ifndef _FANMANAGER_HPP_
#define _FANMANAGER_HPP_

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include <FanRevision.hpp>
#include <ProjectConfig.hpp>
#include <atomic>
#include <cstdint>
#include <esp_log.h>
#include <memory>

class FanCurve;

/// Everything the API needs to describe the fan without guessing.
struct FanStatus
{
    uint8_t duty_percent = 0;
    uint8_t min_percent = 0;
    uint8_t max_percent = 100;
    bool can_turn_off = true;
    bool initialized = false;
    bool fallback_active = false;  ///< revision unknown, running the safe curve
    const char* curve = "none";
    const char* revision = "unknown";
    const char* revision_source = "not-detected";  ///< measured | override | not-detected
    int sample_a_mv = -1;
    int sample_b_mv = -1;
};

/**
 * @brief Owns the fan output: which characteristic applies, what an operator is
 *        allowed to ask for, and how the fan is brought up.
 *
 * The characteristic itself lives in FanCurve. This class is the only authority
 * on the permitted range - the command layer asks it rather than reproducing
 * the bounds, because with a per-revision lower limit two copies would drift.
 */
class FanManager
{
   public:
    FanManager(gpio_num_t fan_pin, std::shared_ptr<ProjectConfig> deviceConfig);

    void setup();
    void setFanDutyCycle(uint8_t dutyPercent);
    uint8_t getFanDutyCycle() const;

    /// Range authority. The command layer rejects out-of-range values, which is
    /// the behaviour callers have always seen, so these must agree with it.
    uint8_t minAllowedPercent() const;
    uint8_t maxAllowedPercent() const;
    bool isPercentAllowed(int percent) const;

    FanStatus status() const;

    /// Bench access: writes the LEDC duty directly and bypasses the curve, so a
    /// characteristic can be measured without trusting the characteristic.
    /// Not persisted; the next setFanDutyCycle() overrides it.
    bool setRawDuty(uint32_t raw);
    uint32_t maxRawDuty() const;

   private:
    /// Clamp into the permitted range, tolerating a Kconfig where MIN > MAX -
    /// the behaviour the old clampFanDuty() had.
    uint8_t clampToAllowed(int percent) const;
    void applyPercent(uint8_t percent);
    void finishKickstart();
    static void kickTimerCallback(void* arg);

    gpio_num_t fan_pin;
    std::shared_ptr<ProjectConfig> deviceConfig;
    bool initialized = false;

    const FanCurve* curve = nullptr;
    FanRevisionResult detection{};
    bool revision_from_override = false;
    bool fallback_active = false;

    esp_timer_handle_t kick_timer = nullptr;
    std::atomic<bool> kick_pending{false};
    uint8_t kick_target_percent = 0;
};

#endif
