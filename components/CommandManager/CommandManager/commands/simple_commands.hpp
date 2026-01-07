#ifndef SIMPLE_COMMANDS
#define SIMPLE_COMMANDS

#include <nlohmann-json.hpp>
#include <string>
#include "CommandResult.hpp"
#include "esp_log.h"
#include "main_globals.hpp"

CommandResult PingCommand();
CommandResult PauseCommand(const nlohmann::json& json);

#endif