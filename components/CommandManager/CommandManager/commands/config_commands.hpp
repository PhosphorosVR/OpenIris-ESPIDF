#include <ProjectConfig.hpp>
#include <memory>
#include <nlohmann-json.hpp>
#include <optional>
#include <string>
#include "CommandResult.hpp"
#include "CommandSchema.hpp"
#include "DependencyRegistry.hpp"

CommandResult saveConfigCommand(std::shared_ptr<DependencyRegistry> registry);
CommandResult getConfigCommand(std::shared_ptr<DependencyRegistry> registry);

CommandResult resetConfigCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);