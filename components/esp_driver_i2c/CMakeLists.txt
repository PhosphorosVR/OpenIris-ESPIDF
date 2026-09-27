# OpenIris: ESP-IDF v5.4.2's esp_driver_i2c with one backported fix (bounded bus-busy
# wait after a NACK, as in v5.5). It shadows the IDF component, so it must match the
# IDF it was copied from. Drop it when moving to v5.5 or later.
if(NOT "$ENV{IDF_VERSION}" STREQUAL "5.4.2")
    message(FATAL_ERROR "components/esp_driver_i2c is a patched copy of ESP-IDF 5.4.2, but IDF "
                        "'$ENV{IDF_VERSION}' is in use. Remove the copy (v5.5+ has the fix) or port it again.")
endif()

idf_build_get_property(target IDF_TARGET)

set(srcs)
set(include "include")

# I2C related source files
if(CONFIG_SOC_I2C_SUPPORTED)
    list(APPEND srcs
                     "i2c_master.c"
                     "i2c_common.c"
        )
    if(CONFIG_SOC_I2C_SUPPORT_SLAVE)
        if(CONFIG_I2C_ENABLE_SLAVE_DRIVER_VERSION_2)
            list(APPEND srcs "i2c_slave_v2.c")
        else()
            list(APPEND srcs "i2c_slave.c")
        endif()
    endif()

endif()

if(${target} STREQUAL "linux")
    set(priv_requires esp_ringbuf)
else()
    set(priv_requires esp_driver_gpio esp_pm esp_ringbuf)
endif()

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${include}
                    PRIV_REQUIRES "${priv_requires}"
                    LDFRAGMENTS "linker.lf"
                    )
