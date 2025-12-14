set (
  requires
  esp_timer
  esp32-camera
  StateManager
  CameraManager
  Helpers
)

if ("$ENV{IDF_TARGET}" STREQUAL "esp32s3")
list(APPEND requires
            usb_device_uvc
)
endif()

idf_component_register(SRCS "UVCStream/UVCStream.cpp"
  INCLUDE_DIRS "UVCStream"
  REQUIRES ${requires}
)