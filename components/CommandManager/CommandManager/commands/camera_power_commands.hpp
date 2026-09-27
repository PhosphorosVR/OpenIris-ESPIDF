#ifndef CAMERA_POWER_COMMANDS_HPP
#define CAMERA_POWER_COMMANDS_HPP
#include <CameraManager.hpp>
#include <memory>
#include <nlohmann-json.hpp>
#include "CommandResult.hpp"
#include "DependencyRegistry.hpp"

// Only built with CONFIG_CAMERA_STATUS (see CMakeLists.txt).
CommandResult getCameraStatusCommand(std::shared_ptr<DependencyRegistry> registry);

#endif
