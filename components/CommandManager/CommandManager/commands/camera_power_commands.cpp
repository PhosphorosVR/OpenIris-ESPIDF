#include "camera_power_commands.hpp"

CommandResult getCameraStatusCommand(std::shared_ptr<DependencyRegistry> registry)
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

    return CommandResult::getSuccessResult(result);
}

#if CONFIG_CAMERA_POWER_CONTROL
namespace
{
constexpr int64_t kMaxOffMs = 30000;  // long enough to read 1V5_Cx with a DMM

nlohmann::json toJson(const RailPoint& point)
{
    return nlohmann::json::array({point.t_us, point.mv[0], point.mv[1]});
}
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

    PowerCycleRequest request{};
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

    const PowerCycleReport report = cameraManager->powerCycle(request);

    auto result = nlohmann::json{
        {"result", report.failed_step ? "failed" : "power_cycled"},
        {"forced", request.force},
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
        {"camera_state", cameraRunStateName(cameraManager->getStatus().state)},
        // bytes; sizing input for the recovery worker (AP3)
        {"stack_free_min", uxTaskGetStackHighWaterMark(nullptr)},
    };
    if (report.failed_step)
    {
        result["failed_step"] = report.failed_step;
    }
    if (request.trace)
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

    return report.failed_step ? CommandResult::getErrorResult(result) : CommandResult::getSuccessResult(result);
}
#endif
