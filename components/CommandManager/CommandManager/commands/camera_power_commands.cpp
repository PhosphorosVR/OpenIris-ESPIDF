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
