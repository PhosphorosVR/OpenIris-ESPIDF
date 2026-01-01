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

set(
  requires
  Helpers
)

# Platform-specific dependencies
if ("$ENV{IDF_TARGET}" STREQUAL "esp32s3" OR "$ENV{IDF_TARGET}" STREQUAL "esp32")
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
if ("$ENV{IDF_TARGET}" STREQUAL "esp32s3" OR "$ENV{IDF_TARGET}" STREQUAL "esp32")
# Common ADC implementation
list(APPEND source_files
  "Monitoring/AdcSampler.cpp"
  )

# Platform-specific GPIO-to-channel mapping
if ("$ENV{IDF_TARGET}" STREQUAL "esp32s3")
  list(APPEND source_files
    "Monitoring/AdcSampler_esp32s3.cpp"
  )
elseif ("$ENV{IDF_TARGET}" STREQUAL "esp32")
  list(APPEND source_files
    "Monitoring/AdcSampler_esp32.cpp"
  )
endif()
endif()


idf_component_register(SRCS ${source_files}
  INCLUDE_DIRS "Monitoring"
  REQUIRES ${requires}
)