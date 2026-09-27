// Only built with CONFIG_CAMERA_RECOVERY_ENABLE (see CMakeLists.txt).
// Recovery policy: level choice, budget, counters, automatic triggers
// (docs/CAM_CE_RESET_Analyse.md, section 8). The restart itself is CameraCycle.cpp.
#include "CameraManager.hpp"

#include <cstring>
#include "esp_attr.h"
#include "esp_timer.h"

static const char* CAMERA_RECOVERY_TAG = "[CAMERA_RECOVERY]";

namespace
{
constexpr int64_t kCooldownUs = 5 * 1000 * 1000LL;  // after every attempt, manual ones too
constexpr uint32_t kSuspendAfterFailures = 3;       // in a row; suspends the automatic triggers
constexpr int kRateMaxAttempts = 10;                // automatic attempts ...
constexpr int64_t kRateWindowUs = 10 * 60 * 1000 * 1000LL;  // ... per ten minutes
constexpr uint32_t kRecoveryOffMs = 500;            // minimum CE low time, adaptive beyond
#if CONFIG_CAMERA_AUTO_RECOVERY
// One more than the plan (F13): on a camera with a damaged flex cable one restart in ten
// failed at the first frame and the next one always worked.
constexpr int kBootAttempts = 2;
#endif
#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
// Long enough for the log manager's periodic flush to store the WARN line first.
#if CONFIG_DEBUG_LOG_ENABLE
constexpr uint32_t kRestartDelayMs = CONFIG_DEBUG_LOG_FLUSH_INTERVAL_MS + 2000;
#else
constexpr uint32_t kRestartDelayMs = 500;
#endif
constexpr uint32_t kRestartMagic = 0x43414d52;  // "CAMR"
#endif

bool isAutomatic(const RecoveryTrigger trigger)
{
    return trigger == RecoveryTrigger::FrameTimeout || trigger == RecoveryTrigger::BootFailure;
}
}  // namespace

#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
// Survives a software reset and needs no flash write.
RTC_NOINIT_ATTR static uint32_t s_restart_marker;
#endif

const char* recoveryRefusalName(const RecoveryRefusal refusal)
{
    switch (refusal)
    {
        case RecoveryRefusal::Cooldown:
            return "cooldown";
        case RecoveryRefusal::Suspended:
            return "suspended";
        case RecoveryRefusal::RateLimited:
            return "rate_limited";
        default:
            return "busy";
    }
}

RecoveryLevel CameraManager::strongestLevel() const
{
#if CONFIG_CAMERA_POWER_CONTROL
    if (camLines.available())
    {
        return RecoveryLevel::PowerCycle;
    }
#endif
    return RecoveryLevel::Reinit;
}

CycleRequest CameraManager::recoveryRequest(const RecoveryLevel level) const
{
    CycleRequest request{};
    request.level = level;
    request.off_ms = kRecoveryOffMs;
    request.adaptive_off = true;
    return request;
}

bool CameraManager::recover(const bool level_auto, const RecoveryLevel level, CycleReport& report)
{
    const RecoveryLevel chosen = level_auto ? this->strongestLevel() : level;
    if (this->runCycleBlocking(this->recoveryRequest(chosen), RecoveryTrigger::Command, report))
    {
        return true;
    }
    portENTER_CRITICAL(&stats_lock);
    ++stats.refused[static_cast<int>(RecoveryRefusal::Busy)];
    portEXIT_CRITICAL(&stats_lock);
    return false;
}

RecoveryStats CameraManager::recoveryStats() const
{
    portENTER_CRITICAL(&stats_lock);
    const RecoveryStats copy = stats;
    portEXIT_CRITICAL(&stats_lock);
    return copy;
}

void CameraManager::onFrameMissing(const uint32_t in_row)
{
    frames_missing.fetch_add(1, std::memory_order_relaxed);
#if CONFIG_CAMERA_AUTO_RECOVERY
    // During a restart the gate is closed on purpose.
    if (in_cycle || cycle_busy || in_row < CONFIG_CAMERA_AUTO_RECOVERY_MISSED_FRAMES)
    {
        return;
    }
    this->submitCycle(this->recoveryRequest(this->strongestLevel()), RecoveryTrigger::FrameTimeout);
#else
    (void)in_row;
#endif
}

// Camera task only. nullptr: go ahead.
const char* CameraManager::admit(const RecoveryTrigger trigger)
{
    if (trigger == RecoveryTrigger::Bench)
    {
        return nullptr;
    }
    const int64_t now = esp_timer_get_time();
    RecoveryRefusal refusal = RecoveryRefusal::Count;
    if (last_attempt_us != 0 && now - last_attempt_us < kCooldownUs)
    {
        refusal = RecoveryRefusal::Cooldown;
    }
    else if (isAutomatic(trigger))
    {
        int recent = 0;
        for (const int64_t t : auto_attempts_us)
        {
            if (t != 0 && now - t < kRateWindowUs)
            {
                ++recent;
            }
        }
        if (stats.suspended)
        {
            refusal = RecoveryRefusal::Suspended;
        }
        else if (recent >= kRateMaxAttempts)
        {
            refusal = RecoveryRefusal::RateLimited;
        }
    }

    if (refusal == RecoveryRefusal::Count)
    {
        if (isAutomatic(trigger))
        {
            rate_warned = false;
        }
        return nullptr;
    }
    portENTER_CRITICAL(&stats_lock);
    ++stats.refused[static_cast<int>(refusal)];
    portEXIT_CRITICAL(&stats_lock);
    if (refusal == RecoveryRefusal::RateLimited && !rate_warned)
    {
        rate_warned = true;
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Automatic camera recovery paused: %d attempts within 10 min", kRateMaxAttempts);
    }
    return recoveryRefusalName(refusal);
}

// Camera task only, after every restart that ran.
void CameraManager::recordCycle(const RecoveryTrigger trigger, const CycleReport& report)
{
    // Bench cycles and levels the board does not have are no recoveries.
    if (trigger == RecoveryTrigger::Bench || (report.failed_step && std::strcmp(report.failed_step, "not_supported") == 0))
    {
        return;
    }

    const int64_t now = esp_timer_get_time();
    last_attempt_us = now;
    if (isAutomatic(trigger))
    {
        auto_attempts_us[auto_attempts_next] = now;
        auto_attempts_next = (auto_attempts_next + 1) % (sizeof(auto_attempts_us) / sizeof(auto_attempts_us[0]));
    }

    const bool success = report.failed_step == nullptr;
    RecoveryEntry entry{};
    entry.uptime_s = static_cast<uint32_t>(now / 1000000);
    entry.trigger = trigger;
    entry.level = report.level;
    entry.rail = report.rail;
    entry.failed_step = report.failed_step;
    entry.off_ms = report.off_ms;
    entry.first_frame_ms = report.first_frame_ms;
    entry.duration_ms = report.duration_ms;

    bool suspended_now = false;
    uint32_t failures = 0;
    portENTER_CRITICAL(&stats_lock);
    RecoveryCounter& by_trigger = stats.by_trigger[static_cast<int>(trigger)];
    RecoveryCounter& by_level = stats.by_level[static_cast<int>(report.level)];
    ++by_trigger.attempts;
    ++by_level.attempts;
    if (success)
    {
        ++by_trigger.successes;
        ++by_level.successes;
    }
    ++stats.rail[static_cast<int>(report.rail)];
    if (success)
    {
        stats.consecutive_failures = 0;
        stats.suspended = false;
    }
    else if (++stats.consecutive_failures >= kSuspendAfterFailures && !stats.suspended)
    {
        stats.suspended = true;
        ++stats.suspensions;
        suspended_now = true;
    }
    failures = stats.consecutive_failures;
    stats.last[stats.last_next] = entry;
    stats.last_next = (stats.last_next + 1) % RecoveryStats::kLast;
    if (stats.last_count < RecoveryStats::kLast)
    {
        ++stats.last_count;
    }
    portEXIT_CRITICAL(&stats_lock);

    if (success)
    {
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Recovery ok: %s, %s, rail %s, %lu ms", recoveryTriggerName(trigger), recoveryLevelName(report.level),
                 railVerdictName(report.rail), static_cast<unsigned long>(report.duration_ms));
    }
    else
    {
        ESP_LOGE(CAMERA_RECOVERY_TAG, "Recovery failed at %s: %s, %s, rail %s, %lu ms", report.failed_step, recoveryTriggerName(trigger),
                 recoveryLevelName(report.level), railVerdictName(report.rail), static_cast<unsigned long>(report.duration_ms));
    }

    if (suspended_now)
    {
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Automatic camera recovery suspended after %lu failures in a row", static_cast<unsigned long>(failures));
#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Restarting the ESP in %lu ms as the last resort", static_cast<unsigned long>(kRestartDelayMs));
        s_restart_marker = kRestartMagic;
        vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
        esp_restart();
#endif
    }
}

void CameraManager::checkRestartMarker()
{
#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
    if (s_restart_marker == kRestartMagic && status.reset_reason == ESP_RST_SW)
    {
        stats.restarted_by_recovery = true;
        ESP_LOGW(CAMERA_RECOVERY_TAG, "This boot follows an ESP restart by the camera recovery");
    }
    s_restart_marker = 0;
#endif
}

#if CONFIG_CAMERA_AUTO_RECOVERY
bool CameraManager::recoverBootFailure()
{
    // Only the very first init, never from inside a restart (setupCamera runs there too).
    if (in_cycle || boot_recovery_done)
    {
        return false;
    }
    boot_recovery_done = true;
    ESP_LOGW(CAMERA_RECOVERY_TAG, "Camera init failed at boot, restarting the camera");
    for (int attempt = 1; attempt <= kBootAttempts; ++attempt)
    {
        CycleReport report{};
        if (!this->runCycleBlocking(this->recoveryRequest(this->strongestLevel()), RecoveryTrigger::BootFailure, report))
        {
            return false;
        }
        if (!report.failed_step)
        {
            return true;
        }
        if (attempt < kBootAttempts)
        {
            // Let the cooldown pass, otherwise the next attempt is refused.
            vTaskDelay(pdMS_TO_TICKS(kCooldownUs / 1000) + 1);
        }
    }
    return false;
}
#endif

#if CONFIG_CAMERA_TEST_HOOKS
const char* CameraManager::injectFault(const char* kind)
{
    if (std::strcmp(kind, "hold_reset") == 0)
    {
#if CONFIG_CAMERA_POWER_CONTROL
        // Rev.5: the sensor stops mid-stream; the next restart releases the line.
        return camLines.holdReset(false) == LineOutcome::Done ? nullptr : "not_supported";
#else
        return "not_supported";
#endif
    }
    if (std::strcmp(kind, "sensor_standby") == 0)
    {
        // SYSTEM CTROL0 bit 6, software power down (OV3660 DS p. 36): frames stop, SCCB stays up.
        xSemaphoreTake(sensor_mutex, portMAX_DELAY);
        const int ret = camera_sensor ? camera_sensor->set_reg(camera_sensor, 0x3008, 0x40, 0x40) : -1;
        xSemaphoreGive(sensor_mutex);
        return ret == 0 ? nullptr : "sccb_failed";
    }
    return "unknown_kind";
}
#endif
