// Only built with CONFIG_CAMERA_RECOVERY_ENABLE (see CMakeLists.txt).
// Recovery policy: level choice, budget, counters, automatic triggers
// (docs/CAM_CE_RESET_Analyse.md, sections 8 and 17.9). The restart itself is CameraCycle.cpp.
#include "CameraManager.hpp"

#include <cstring>
#include "esp_attr.h"
#include "esp_timer.h"

static const char* CAMERA_RECOVERY_TAG = "[CAMERA_RECOVERY]";

namespace
{
constexpr int64_t kCooldownUs = 5 * 1000 * 1000LL;  // after every attempt, manual ones too
// A restart held when frames kept coming for this long. Held restarts cost no budget;
// three in a row that failed or did not hold suspend the automatic triggers, until
// the quiet time passes without a further failure.
constexpr int64_t kHoldUs = 30 * 1000 * 1000LL;
constexpr uint32_t kSuspendAfterUnheld = 3;
constexpr int64_t kResumeAfterUs = 5 * 60 * 1000 * 1000LL;
constexpr uint32_t kRecoveryOffMs = 500;  // minimum CE low time, adaptive beyond
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

void CameraManager::onFrameDelivered()
{
    // One load per frame while no restart is watched.
    if (!watching.load(std::memory_order_relaxed))
    {
        return;
    }
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&stats_lock);
    if (watching.load(std::memory_order_relaxed))
    {
        if (watch_first_frame_us == 0)
        {
            watch_first_frame_us = now;
        }
        else if (now - watch_first_frame_us >= kHoldUs)
        {
            this->endWatchLocked(true, now);
        }
    }
    portEXIT_CRITICAL(&stats_lock);
}

// Under stats_lock. true: this suspends the automatic triggers.
bool CameraManager::endWatchLocked(const bool held, const int64_t now)
{
    watching.store(false, std::memory_order_relaxed);
    // The watched restart is the newest entry: every restart ends the previous watch.
    stats.last[(stats.last_next + RecoveryStats::kLast - 1) % RecoveryStats::kLast].held = held ? 1 : 0;
    if (!held)
    {
        ++stats.not_held;
        return this->countUnheldLocked(now);
    }
    ++stats.held;
    stats.unheld_in_row = 0;
    return false;
}

// Under stats_lock: a restart failed or did not hold. true: this suspends the automatic triggers.
bool CameraManager::countUnheldLocked(const int64_t now)
{
    last_unheld_us = now;
    if (++stats.unheld_in_row < kSuspendAfterUnheld || stats.suspended)
    {
        return false;
    }
    stats.suspended = true;
    ++stats.suspensions;
    return true;
}

// Camera task only.
void CameraManager::onSuspended()
{
    ESP_LOGW(CAMERA_RECOVERY_TAG, "Automatic camera recovery suspended after %lu restarts in a row that did not hold, resumes after %d min without a failure",
             static_cast<unsigned long>(kSuspendAfterUnheld), static_cast<int>(kResumeAfterUs / 60000000));
#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
    ESP_LOGW(CAMERA_RECOVERY_TAG, "Restarting the ESP in %lu ms as the last resort", static_cast<unsigned long>(kRestartDelayMs));
    s_restart_marker = kRestartMagic;
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    esp_restart();
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
    bool not_held = false;
    bool suspended_now = false;
    bool resumed = false;
    portENTER_CRITICAL(&stats_lock);
    // Frames stopped before the last restart had held.
    if (trigger == RecoveryTrigger::FrameTimeout && watching.load(std::memory_order_relaxed))
    {
        not_held = true;
        suspended_now = this->endWatchLocked(false, now);
    }
    if (last_attempt_us != 0 && now - last_attempt_us < kCooldownUs)
    {
        refusal = RecoveryRefusal::Cooldown;
    }
    else if (isAutomatic(trigger) && stats.suspended)
    {
        if (now - last_unheld_us < kResumeAfterUs)
        {
            refusal = RecoveryRefusal::Suspended;
        }
        else
        {
            stats.suspended = false;
            stats.unheld_in_row = 0;
            ++stats.resumes;
            resumed = true;
        }
    }
    if (refusal != RecoveryRefusal::Count)
    {
        ++stats.refused[static_cast<int>(refusal)];
    }
    portEXIT_CRITICAL(&stats_lock);

    if (not_held)
    {
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Recovery did not hold: camera lost again within %d s", static_cast<int>(kHoldUs / 1000000));
    }
    if (suspended_now)
    {
        this->onSuspended();
    }
    if (resumed)
    {
        ESP_LOGW(CAMERA_RECOVERY_TAG, "Automatic camera recovery resumed after %d min without a failure", static_cast<int>(kResumeAfterUs / 60000000));
    }
    return refusal == RecoveryRefusal::Count ? nullptr : recoveryRefusalName(refusal);
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

    const bool success = report.failed_step == nullptr;
    RecoveryEntry entry{};
    entry.uptime_s = static_cast<uint32_t>(now / 1000000);
    entry.trigger = trigger;
    entry.level = report.level;
    entry.rail = report.rail;
    entry.failed_step = report.failed_step;
    entry.held = success ? -1 : 0;
    entry.off_ms = report.off_ms;
    entry.first_frame_ms = report.first_frame_ms;
    entry.duration_ms = report.duration_ms;

    bool suspended_now = false;
    portENTER_CRITICAL(&stats_lock);
    RecoveryCounter& by_trigger = stats.by_trigger[static_cast<int>(trigger)];
    RecoveryCounter& by_level = stats.by_level[static_cast<int>(report.level)];
    ++by_trigger.attempts;
    ++by_level.attempts;
    ++stats.rail[static_cast<int>(report.rail)];
    stats.last[stats.last_next] = entry;
    stats.last_next = (stats.last_next + 1) % RecoveryStats::kLast;
    if (stats.last_count < RecoveryStats::kLast)
    {
        ++stats.last_count;
    }
    // A new restart ends the watch of the previous one without a verdict.
    watching.store(false, std::memory_order_relaxed);
    if (success)
    {
        ++by_trigger.successes;
        ++by_level.successes;
        if (trigger == RecoveryTrigger::Command)
        {
            // Restarted on purpose: the suspension and the count start over.
            stats.suspended = false;
            stats.unheld_in_row = 0;
        }
        watch_first_frame_us = 0;
        watching.store(true, std::memory_order_relaxed);
    }
    else
    {
        suspended_now = this->countUnheldLocked(now);
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
        this->onSuspended();
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
