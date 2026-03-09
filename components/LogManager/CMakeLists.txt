set(
  requires
  Helpers
  spiffs
)

set(
  source_files
  "LogManager/LogManager.cpp"
)

idf_component_register(SRCS ${source_files}
  INCLUDE_DIRS "LogManager"
  REQUIRES ${requires}
)
