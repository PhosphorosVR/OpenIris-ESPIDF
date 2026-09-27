#include "camera_power_commands.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace
{
nlohmann::json toJson(const RailPoint& point)
{
    return nlohmann::json::array({point.t_us, point.mv[0], point.mv[1]});
}

#if CONFIG_CAMERA_RECOVERY_ENABLE
nlohmann::json counterJson(const RecoveryCounter& counter)
{
    return {{"attempts", counter.attempts}, {"successes", counter.successes}};
}
#endif
}  // namespace

nlohmann::json cycleReportJson(const CycleReport& report)
{
    auto result = nlohmann::json{
        {"level", recoveryLevelName(report.level)},
        {"steps",
         {
             {"power", lineOutcomeName(report.power)},
             {"reset", lineOutcomeName(report.reset)},
             {"reinit", esp_err_to_name(report.reinit)},
             {"first_frame", report.first_frame},
         }},
        {"rail",
         {
             {"verdict", railVerdictName(report.rail)},
             {"control_mv", {report.control_mv[0], report.control_mv[1]}},
             {"end_mv", {report.end_mv[0], report.end_mv[1]}},
             {"off_ms", report.off_ms},
             {"rise_us", report.rise_us},
         }},
        {"pid_before", report.pid_before},
        {"pid_after", report.pid_after},
        {"first_frame_ms", report.first_frame_ms},
        {"duration_ms", report.duration_ms},
        // bytes left on the camera task
        {"stack_free_min", report.stack_free_min},
    };
    if (report.failed_step)
    {
        result["failed_step"] = report.failed_step;
    }
    if (!report.fall.empty() || !report.rise.empty())
    {
        // [t_us, pad A mV, pad B mV]; t from CE low (fall) and from CE release (rise)
        auto fall = nlohmann::json::array();
        for (const auto& point : report.fall)
        {
            fall.push_back(toJson(point));
        }
        auto rise = nlohmann::json::array();
        for (const auto& point : report.rise)
        {
            rise.push_back(toJson(point));
        }
        result["trace"] = {{"fall", fall}, {"rise", rise}};
    }
    return result;
}

CommandResult getCameraStatusCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json)
{
    const auto cameraManager = registry->resolve<CameraManager>(DependencyType::camera_manager);
    if (!cameraManager)
    {
        return CommandResult::getErrorResult("Camera manager unavailable");
    }

    const CameraStatus status = cameraManager->getStatus();
    auto result = nlohmann::json{
        {"state", cameraRunStateName(status.state)},
        {"init", esp_err_to_name(status.init_result)},
        {"pid", status.pid},
        {"reset_reason", resetReasonName(status.reset_reason)},
        {"uptime_s", esp_timer_get_time() / 1000000},
        {"heap_internal",
         {
             {"free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)},
             {"largest_block", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)},
         }},
    };

#if CONFIG_CAMERA_POWER_CONTROL
    const CamLines& lines = cameraManager->lines();
    const LineProbe& probe = lines.lastProbe();
    result["lines"] = {
        {"presence", linePresenceName(probe.presence)},
        {"probe_mv",
         {
             {"reset_pullup", probe.reset_pullup_mv},
             {"reset_pulldown", probe.reset_pulldown_mv},
             {"ce", probe.ce_mv},
         }},
        {"power", lines.powerHeldOff() ? "held_off" : "released"},
        {"reset", lines.resetHeld() ? "held" : "released"},
    };
#endif

#if CONFIG_CAMERA_RECOVERY_ENABLE
    const RecoveryStats stats = cameraManager->recoveryStats();
    nlohmann::json by_trigger = nlohmann::json::object();
    for (int i = 0; i < RecoveryStats::kTriggers; ++i)
    {
        by_trigger[recoveryTriggerName(static_cast<RecoveryTrigger>(i))] = counterJson(stats.by_trigger[i]);
    }
    by_trigger.erase("bench");
    nlohmann::json by_level = nlohmann::json::object();
    for (int i = 0; i < RecoveryStats::kLevels; ++i)
    {
        by_level[recoveryLevelName(static_cast<RecoveryLevel>(i))] = counterJson(stats.by_level[i]);
    }
    nlohmann::json rail = nlohmann::json::object();
    for (int i = 0; i < RecoveryStats::kVerdicts; ++i)
    {
        rail[railVerdictName(static_cast<RailVerdict>(i))] = stats.rail[i];
    }
    nlohmann::json refused = nlohmann::json::object();
    for (int i = 0; i < static_cast<int>(RecoveryRefusal::Count); ++i)
    {
        refused[recoveryRefusalName(static_cast<RecoveryRefusal>(i))] = stats.refused[i];
    }
    // newest first
    nlohmann::json last = nlohmann::json::array();
    for (int n = 0; n < stats.last_count; ++n)
    {
        const RecoveryEntry& e = stats.last[(stats.last_next + RecoveryStats::kLast - 1 - n) % RecoveryStats::kLast];
        last.push_back({
            {"uptime_s", e.uptime_s},
            {"trigger", recoveryTriggerName(e.trigger)},
            {"level", recoveryLevelName(e.level)},
            {"result", e.failed_step ? e.failed_step : "ok"},
            {"rail", railVerdictName(e.rail)},
            {"off_ms", e.off_ms},
            {"first_frame_ms", e.first_frame_ms},
            {"duration_ms", e.duration_ms},
        });
    }
#if CONFIG_CAMERA_AUTO_RECOVERY
    constexpr bool automatic = true;
#else
    constexpr bool automatic = false;
#endif
    result["recovery"] = {
        {"automatic", automatic},
        {"strongest_level", recoveryLevelName(cameraManager->strongestLevel())},
        {"suspended", stats.suspended},
        {"consecutive_failures", stats.consecutive_failures},
        {"suspensions", stats.suspensions},
        {"by_trigger", by_trigger},
        {"by_level", by_level},
        {"rail", rail},
        {"refused", refused},
        {"frames_missing", status.frames_missing},
        {"restarted_by_recovery", stats.restarted_by_recovery},
        {"last", last},
    };
    static constexpr const char* kReclaimNames[] = {"none", "returned", "streaming", "no_frame"};
    const CameraGateState gate = cameraGateState();
    result["gate"] = {
        {"open", gate.open},
        {"in_driver", gate.in_driver},
        {"frames_out", gate.frames_out},
        {"last_reclaim", kReclaimNames[static_cast<int>(gate.last_reclaim)]},
        {"drain_timeouts", gate.drain_timeouts},
        {"taken_back", gate.taken_back},
        {"at_timeout", {{"in_driver", gate.timeout_in_driver}, {"frames_out", gate.timeout_frames_out}}},
    };

    // End of a test run: one WARN line, which the log manager's normal flush stores in the
    // persistent log. No flash write of its own.
    if (json.value("persist", false))
    {
        uint32_t attempts = 0;
        uint32_t successes = 0;
        for (const auto& counter : stats.by_trigger)
        {
            attempts += counter.attempts;
            successes += counter.successes;
        }
        ESP_LOGW("[CAMERA_RECOVERY]",
                 "Summary: %lu/%lu ok (command %lu/%lu, frame_timeout %lu/%lu, boot_failure %lu/%lu), rail collapsed %lu partial %lu "
                 "not_collapsed %lu inconclusive %lu, suspensions %lu, refused %lu/%lu/%lu/%lu, frames missing %lu, reset %s%s",
                 static_cast<unsigned long>(successes), static_cast<unsigned long>(attempts),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::Command)].successes),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::Command)].attempts),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::FrameTimeout)].successes),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::FrameTimeout)].attempts),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::BootFailure)].successes),
                 static_cast<unsigned long>(stats.by_trigger[static_cast<int>(RecoveryTrigger::BootFailure)].attempts),
                 static_cast<unsigned long>(stats.rail[static_cast<int>(RailVerdict::Collapsed)]),
                 static_cast<unsigned long>(stats.rail[static_cast<int>(RailVerdict::Partial)]),
                 static_cast<unsigned long>(stats.rail[static_cast<int>(RailVerdict::NotCollapsed)]),
                 static_cast<unsigned long>(stats.rail[static_cast<int>(RailVerdict::Inconclusive)]), static_cast<unsigned long>(stats.suspensions),
                 static_cast<unsigned long>(stats.refused[0]), static_cast<unsigned long>(stats.refused[1]),
                 static_cast<unsigned long>(stats.refused[2]), static_cast<unsigned long>(stats.refused[3]),
                 static_cast<unsigned long>(status.frames_missing), resetReasonName(status.reset_reason),
                 stats.restarted_by_recovery ? " (by recovery)" : "");
        result["persisted"] = true;
    }
#else
    (void)json;
#endif

    return CommandResult::getSuccessResult(result);
}

#if CONFIG_CAMERA_POWER_CONTROL
namespace
{
constexpr int64_t kMaxOffMs = 30000;  // long enough to read 1V5_Cx with a DMM
}  // namespace

CommandResult cameraPowerCycleCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json)
{
    // A running stream holds frames and the sensor pointer. The bench cycle is meant for
    // setup mode or after a failed camera boot, where nothing consumes frames.
    if (getUsbHandoverDone())
    {
        return CommandResult::getErrorResult({{"error", "busy"}, {"reason", "UVC streaming is running, switch to setup mode"}});
    }
#if CONFIG_GENERAL_ENABLE_WIRELESS
    if (const auto projectConfig = registry->resolve<ProjectConfig>(DependencyType::project_config);
        projectConfig && projectConfig->getDeviceMode() == StreamingMode::WIFI)
    {
        return CommandResult::getErrorResult({{"error", "busy"}, {"reason", "WiFi streaming mode, switch to setup mode"}});
    }
#endif

    CycleRequest request{};
    request.level = RecoveryLevel::PowerCycle;
    if (json.contains("off_ms"))
    {
        if (!json["off_ms"].is_number_integer() || json["off_ms"].get<int64_t>() < 1 || json["off_ms"].get<int64_t>() > kMaxOffMs)
        {
            return CommandResult::getErrorResult("off_ms must be an integer from 1 to 30000");
        }
        request.off_ms = static_cast<uint32_t>(json["off_ms"].get<int64_t>());
    }
    for (const auto* key : {"trace", "force"})
    {
        if (json.contains(key) && !json[key].is_boolean())
        {
            return CommandResult::getErrorResult(std::string(key) + " must be a boolean");
        }
    }
    request.trace = json.value("trace", false);
    request.force = json.value("force", false);

    const auto cameraManager = registry->resolve<CameraManager>(DependencyType::camera_manager);
    if (!cameraManager)
    {
        return CommandResult::getErrorResult("Camera manager unavailable");
    }
    // Strict: without force nothing is touched on a board the probe did not confirm.
    if (!cameraManager->lines().available() && !request.force)
    {
        return CommandResult::getErrorResult(
            {{"error", "not_supported"}, {"lines", linePresenceName(cameraManager->lines().lastProbe().presence)}});
    }

    CycleReport report{};
    if (!cameraManager->runCycleBlocking(request, RecoveryTrigger::Bench, report))
    {
        return CommandResult::getErrorResult({{"error", "busy"}, {"reason", "another camera restart is running"}});
    }

    auto result = cycleReportJson(report);
    result["result"] = report.failed_step ? "failed" : "power_cycled";
    result["forced"] = request.force;
    result["camera_state"] = cameraRunStateName(cameraManager->getStatus().state);
    return report.failed_step ? CommandResult::getErrorResult(result) : CommandResult::getSuccessResult(result);
}
#endif
