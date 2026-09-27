#include "CommandResult.hpp"
#include "DependencyRegistry.hpp"
#include "OpenIrisTasks.hpp"
#include "ProjectConfig.hpp"
#include "esp_timer.h"
#include "main_globals.hpp"

#include <format>
#include <nlohmann-json.hpp>
#include <string>

CommandResult updateOTACredentialsCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

CommandResult updateLEDDutyCycleCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult getLEDDutyCycleCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult updateFanDutyCycleCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult getFanDutyCycleCommand(std::shared_ptr<DependencyRegistry> registry);

// Fan drive revision and bench access
CommandResult getBoardRevisionCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult setBoardRevisionCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult setFanTuningCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult setFanRawDutyCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

CommandResult restartDeviceCommand();

CommandResult startStreamingCommand();

CommandResult switchModeCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

CommandResult getDeviceModeCommand(std::shared_ptr<DependencyRegistry> registry);

CommandResult getSerialNumberCommand(std::shared_ptr<DependencyRegistry> registry);

// Monitoring
CommandResult getLEDCurrentCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult getBatteryStatusCommand(std::shared_ptr<DependencyRegistry> registry);

// General info
CommandResult getInfoCommand(std::shared_ptr<DependencyRegistry> registry);

// Debug logs
CommandResult getLogsCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult getPersistentLogsCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult clearPersistentLogsCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult setDebugLogEnabledCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult getDebugLogEnabledCommand(std::shared_ptr<DependencyRegistry> registry);