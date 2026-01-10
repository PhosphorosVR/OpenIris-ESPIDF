# Architecture:
# +-----------------------+
# |   MonitoringManager   | ← High-level coordinator
# +-----------------------+
# |  BatteryMonitor       | ← Battery logic (platform-independent)
# |  CurrentMonitor       | ← Current logic (platform-independent)
# +-----------------------+
# |      AdcSampler       | ← BSP: Unified ADC sampling interface
# +-----------------------+
# |   ESP-IDF ADC HAL     | ← Espressif official driver
# +-----------------------+
#
# Supported platforms (must support ESP-CAM):
# - ESP32:    Tested
# - ESP32-S3: Tested
# - ESP32-S2: UNTESTED - Based on datasheet

set(
  requires
  Helpers
)

# List of supported ADC platforms (aligned with ESP_CAMERA_SUPPORTED)
set(ADC_SUPPORTED_TARGETS "esp32" "esp32s3" "esp32s2")

# Check if current target supports ADC
list(FIND ADC_SUPPORTED_TARGETS "$ENV{IDF_TARGET}" ADC_TARGET_INDEX)
if (NOT ADC_TARGET_INDEX EQUAL -1)
  set(ADC_SAMPLER_SUPPORTED TRUE)
else()
  set(ADC_SAMPLER_SUPPORTED FALSE)
endif()

# Platform-specific dependencies
if (ADC_SAMPLER_SUPPORTED)
list(APPEND requires
            driver 
            esp_adc
)
endif()

# Common source files (platform-independent business logic)
set(
  source_files
  "Monitoring/MonitoringManager.cpp"
  "Monitoring/BatteryMonitor.cpp"
  "Monitoring/CurrentMonitor.cpp"
)

# BSP Layer: ADC sampler implementation
if (ADC_SAMPLER_SUPPORTED)
  # Common ADC implementation
  list(APPEND source_files
    "Monitoring/AdcSampler.cpp"
  )

  # Platform-specific GPIO-to-channel mapping and calibration
  if ("$ENV{IDF_TARGET}" STREQUAL "esp32s3")
    list(APPEND source_files "Monitoring/AdcSampler_esp32s3.cpp")
  elseif ("$ENV{IDF_TARGET}" STREQUAL "esp32s2")
    list(APPEND source_files "Monitoring/AdcSampler_esp32s2.cpp")
  elseif ("$ENV{IDF_TARGET}" STREQUAL "esp32")
    list(APPEND source_files "Monitoring/AdcSampler_esp32.cpp")
  endif()
endif()


idf_component_register(SRCS ${source_files}
  INCLUDE_DIRS "Monitoring"
  REQUIRES ${requires}
)