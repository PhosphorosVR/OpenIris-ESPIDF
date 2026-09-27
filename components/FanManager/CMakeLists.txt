# Kconfig values are not available during the build system's early expansion
# pass, so REQUIRES has to stay unconditional. SRCS is only read in the full
# pass - which is where the fan can actually be compiled out: on a board without
# CONFIG_FAN_PWM_ENABLE this component becomes headers only and contributes no
# code to the image at all.
set(fan_srcs "")
if(CONFIG_FAN_PWM_ENABLE)
  set(fan_srcs
    "FanManager/FanManager.cpp"
    "FanManager/FanCurve.cpp"
    "FanManager/FanRevision.cpp"
  )
endif()

idf_component_register(SRCS ${fan_srcs}
  INCLUDE_DIRS "FanManager"
  REQUIRES driver esp_driver_ledc esp_timer ProjectConfig Monitoring
)
