set(command_srcs
    "CommandManager/CommandManager.cpp"
    "CommandManager/CommandResult.cpp"
    "CommandManager/CommandSchema.cpp"
    "CommandManager/commands/simple_commands.cpp"
    "CommandManager/commands/camera_commands.cpp"
    "CommandManager/commands/wifi_commands.cpp"
    "CommandManager/commands/config_commands.cpp"
    "CommandManager/commands/mdns_commands.cpp"
    "CommandManager/commands/device_commands.cpp"
    "CommandManager/commands/scan_commands.cpp"
)
if(CONFIG_CAMERA_STATUS)
  list(APPEND command_srcs "CommandManager/commands/camera_power_commands.cpp")
endif()
if(CONFIG_CAMERA_RECOVERY_ENABLE)
  list(APPEND command_srcs "CommandManager/commands/camera_recovery_commands.cpp")
endif()

idf_component_register(
  SRCS ${command_srcs}
  INCLUDE_DIRS
     "CommandManager"
     "CommandManager/commands"
  REQUIRES ProjectConfig nlohmann-json CameraManager OpenIrisTasks wifiManager Helpers LEDManager FanManager Monitoring LogManager
)
