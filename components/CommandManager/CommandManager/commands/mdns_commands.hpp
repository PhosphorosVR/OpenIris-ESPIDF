#include <ProjectConfig.hpp>
#include <memory>
#include <nlohmann-json.hpp>
#include <optional>
#include <string>
#include "CommandResult.hpp"
#include "CommandSchema.hpp"
#include "DependencyRegistry.hpp"

CommandResult setMDNSCommand(std::shared_ptr<DependencyRegistry> registry, const nlohmann::json& json);
CommandResult getMDNSNameCommand(std::shared_ptr<DependencyRegistry> registry);