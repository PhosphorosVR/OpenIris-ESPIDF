#ifndef CAMERA_POWER_COMMANDS_HPP
#define CAMERA_POWER_COMMANDS_HPP
#include <CameraManager.hpp>
#include <ProjectConfig.hpp>
#include <main_globals.hpp>
#include <memory>
#include <nlohmann-json.hpp>
#include "CommandResult.hpp"
#include "DependencyRegistry.hpp"

// Only built with CONFIG_CAMERA_STATUS (see CMakeLists.txt).
nlohmann::json cycleReportJson(const CycleReport& report);
// {"persist": true} also writes the recovery counters as one WARN line (persistent log).
CommandResult getCameraStatusCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

#if CONFIG_CAMERA_POWER_CONTROL
// Bench: {"off_ms": 1..30000 (500), "trace": false, "force": false}; setup mode only.
CommandResult cameraPowerCycleCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
#endif

#endif
