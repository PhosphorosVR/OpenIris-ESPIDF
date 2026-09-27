#ifndef CAMERA_RECOVERY_COMMANDS_HPP
#define CAMERA_RECOVERY_COMMANDS_HPP
#include <CameraManager.hpp>
#include <memory>
#include <nlohmann-json.hpp>
#include "CommandResult.hpp"
#include "DependencyRegistry.hpp"

// Only built with CONFIG_CAMERA_RECOVERY_ENABLE (see CMakeLists.txt).
// {"level": "auto" (default) | "reinit" | "reset" | "power_cycle"}; works while streaming.
CommandResult recoverCameraCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

#if CONFIG_CAMERA_TEST_HOOKS
// {"kind": "hold_reset" | "sensor_standby"}: stops the frames to exercise the automatic recovery.
CommandResult cameraTestFaultCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
#endif

#endif
