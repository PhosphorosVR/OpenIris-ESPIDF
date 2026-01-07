#ifndef SCAN_COMMANDS_HPP
#define SCAN_COMMANDS_HPP

#include <nlohmann-json.hpp>
#include <string>
#include <wifiManager.hpp>
#include "CommandResult.hpp"
#include "DependencyRegistry.hpp"
#include "esp_log.h"

CommandResult scanNetworksCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);

#endif