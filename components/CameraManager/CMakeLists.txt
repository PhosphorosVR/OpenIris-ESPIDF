set(camera_srcs "CameraManager/CameraManager.cpp")
if(CONFIG_CAMERA_STATUS)
  list(APPEND camera_srcs "CameraManager/CameraStatus.cpp")
endif()
if(CONFIG_CAMERA_POWER_CONTROL)
  list(APPEND camera_srcs "CameraManager/CamLines.cpp")
endif()

idf_component_register(SRCS ${camera_srcs}
  INCLUDE_DIRS "CameraManager"
  REQUIRES esp32-camera StateManager ProjectConfig driver esp_driver_ledc esp_psram
)

# REQUIRES cannot follow Kconfig (see FanManager/CMakeLists.txt), and a new REQUIRES
# edge reorders the link of every board. An optional requirement is only a link
# dependency added in the full pass, so boards without the feature stay untouched.
if(CONFIG_CAMERA_POWER_CONTROL)
  idf_component_optional_requires(PRIVATE Monitoring)
endif()
