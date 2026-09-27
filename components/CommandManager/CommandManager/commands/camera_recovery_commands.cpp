#include "camera_recovery_commands.hpp"
#include <cstring>
#include "camera_power_commands.hpp"

CommandResult recoverCameraCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json)
{
    if (json.contains("level") && !json["level"].is_string())
    {
        return CommandResult::getErrorResult("level must be auto, reinit, reset or power_cycle");
    }
    const std::string requested = json.value("level", std::string("auto"));
    bool level_auto = false;
    RecoveryLevel level = RecoveryLevel::Reinit;
    if (requested == "auto")
    {
        level_auto = true;
    }
    else if (requested == "reinit")
    {
        level = RecoveryLevel::Reinit;
    }
    else if (requested == "reset")
    {
        level = RecoveryLevel::HwReset;
    }
    else if (requested == "power_cycle")
    {
        level = RecoveryLevel::PowerCycle;
    }
    else
    {
        return CommandResult::getErrorResult("level must be auto, reinit, reset or power_cycle");
    }

    const auto cameraManager = registry->resolve<CameraManager>(DependencyType::camera_manager);
    if (!cameraManager)
    {
        return CommandResult::getErrorResult("Camera manager unavailable");
    }

    CycleReport report{};
    if (!cameraManager->recover(level_auto, level, report))
    {
        return CommandResult::getErrorResult({{"error", "busy"}, {"reason", "another camera restart is running"}});
    }

    // Refused before anything was touched: cooldown, or a level this board does not have.
    const char* failed = report.failed_step;
    if (failed && (std::strcmp(failed, "cooldown") == 0 || std::strcmp(failed, "not_supported") == 0))
    {
        return CommandResult::getErrorResult({{"error", failed}, {"requested", requested}});
    }

    auto result = cycleReportJson(report);
    result["result"] = failed ? "failed" : "recovered";
    result["trigger"] = "command";
    result["requested"] = requested;
    result["performed"] = recoveryLevelName(report.level);
    result["degraded"] = level_auto && report.level != RecoveryLevel::PowerCycle;
    result["camera_state"] = cameraRunStateName(cameraManager->getStatus().state);
    return failed ? CommandResult::getErrorResult(result) : CommandResult::getSuccessResult(result);
}

#if CONFIG_CAMERA_TEST_HOOKS
CommandResult cameraTestFaultCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json)
{
    if (!json.contains("kind") || !json["kind"].is_string())
    {
        return CommandResult::getErrorResult("kind must be hold_reset or sensor_standby");
    }
    const auto cameraManager = registry->resolve<CameraManager>(DependencyType::camera_manager);
    if (!cameraManager)
    {
        return CommandResult::getErrorResult("Camera manager unavailable");
    }
    const std::string kind = json["kind"].get<std::string>();
    if (const char* error = cameraManager->injectFault(kind.c_str()))
    {
        return CommandResult::getErrorResult({{"error", error}, {"kind", kind}});
    }
    return CommandResult::getSuccessResult({{"fault", kind}});
}
#endif
