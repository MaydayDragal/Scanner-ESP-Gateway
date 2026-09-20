idf_build_get_property(target IDF_TARGET)
set(IDF_VERSION "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")

set(srcs
    "descriptors_control.c"
    "tinyusb.c"
    "usb_descriptors.c"
    "tinyusb_task.c"
    )

# We don't use Kconfig based conditional as the Kconfig values are not evaluated yet
# when components are being registered. Thus, always add the (private) requirements, regardless of Kconfig.
# IDF version conditionals and Target conditionals are allowed for components
set(priv_req esp_pm)
set(req fatfs vfs)

# USB-PHY driver
# For IDF 6.x and later, usb_phy driver is moved from usb to esp_hw_support
if(IDF_VERSION VERSION_LESS "6.0.0")
    list(APPEND priv_req usb)
else()
    list(APPEND priv_req esp_hw_support)
endif()

if(CONFIG_TINYUSB_CDC_ENABLED)
    list(APPEND srcs
        "cdc.c"
        "tinyusb_cdc_acm.c"
        )
    if(CONFIG_VFS_SUPPORT_IO)
        list(APPEND srcs
            "tinyusb_console.c"
            "vfs_tinyusb.c"
            )
    endif() # CONFIG_VFS_SUPPORT_IO
endif() # CONFIG_TINYUSB_CDC_ENABLED

if(CONFIG_TINYUSB_MSC_ENABLED)
    list(APPEND srcs
        "tinyusb_msc.c"
        "storage_spiflash.c"
        )
    if(CONFIG_SOC_SDMMC_HOST_SUPPORTED)
        list(APPEND srcs
            "storage_sdmmc.c"
            )
    endif() # CONFIG_SOC_SDMMC_HOST_SUPPORTED
endif() # CONFIG_TINYUSB_MSC_ENABLED

if(CONFIG_TINYUSB_NET_MODE_NCM)
    list(APPEND srcs
         "tinyusb_net.c"
         )
endif() # CONFIG_TINYUSB_NET_MODE_NCM

# Software VBUS monitoring is needed for chips with HS USB-OTG peripheral
if(${target} STREQUAL "esp32p4" OR ${target} STREQUAL "esp32s31")
    list(APPEND srcs "tinyusb_vbus_monitor.c")

    # VBUS monitoring requires GPIO driver
    if(IDF_VERSION VERSION_GREATER_EQUAL "5.3")
        list(APPEND priv_req esp_driver_gpio)
    else()
        list(APPEND priv_req driver)
    endif()
endif() # esp32p4/esp32s31

# TinyUSB PM locks
if(CONFIG_TINYUSB_PM)
    list(APPEND srcs "tinyusb_pm.c")
endif()

# USB Device light-sleep wakeup, must be also supported by the target
if(CONFIG_TINYUSB_USB_OTG_WAKEUP)
    list(APPEND srcs "tinyusb_usb_wakeup.c")
endif()

# idf_component.yml is ignored when the component manager is disabled, so the
# dependency on tinyusb must be declared here instead. It belongs in REQUIRES
# (not PRIV_REQUIRES) because the public header tinyusb.h includes tusb.h.
idf_build_get_property(idf_component_manager IDF_COMPONENT_MANAGER)
if(NOT idf_component_manager)
    list(APPEND req tinyusb)
endif()

# Determine whether tinyusb is fetched from component registry or from local path
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb) # Local component
else()
    set(tinyusb_name espressif__tinyusb) # Managed component
endif()

if(CONFIG_TINYUSB_MTP_ENABLED)
    idf_component_get_property(tinyusb_dir ${tinyusb_name} COMPONENT_DIR)
    if(NOT EXISTS "${tinyusb_dir}/src/class/mtp/mtp_device.c")
        message(FATAL_ERROR "CONFIG_TINYUSB_MTP_ENABLED requires TinyUSB MTP device class support.
        Please use a TinyUSB release that provides src/class/mtp/mtp_device.c or disable MTP.")
    endif()
    list(APPEND srcs
         "mtp/tinyusb_mtp.c"
         "mtp/tinyusb_mtp_codec.c"
         "mtp/tinyusb_mtp_object_store.c"
         "mtp/tinyusb_mtp_operations.c"
         "mtp/tinyusb_mtp_properties.c"
         "mtp/tinyusb_mtp_read.c"
         "mtp/tinyusb_mtp_transfer.c"
         "mtp/tinyusb_mtp_transport.c"
         "mtp/tinyusb_mtp_write.c")
endif() # CONFIG_TINYUSB_MTP_ENABLED

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "include_private"
                       PRIV_REQUIRES ${priv_req}
                       REQUIRES ${req}
                       )

# Pass tusb_config.h from this component to TinyUSB
idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "include")

# Preserve the exact managed package. Compile a deterministic, hash-checked MSC
# copy with command-status observation; host tests run this same transformation.
if(CONFIG_TINYUSB_MSC_ENABLED)
    idf_component_get_property(tinyusb_dir ${tinyusb_name} COMPONENT_DIR)
    idf_build_get_property(patch_python PYTHON)
    set(msc_upstream "${tinyusb_dir}/src/class/msc/msc_device.c")
    set(msc_patched "${CMAKE_CURRENT_BINARY_DIR}/scanner_msc_device.c")
    set(msc_patch "${CMAKE_CURRENT_LIST_DIR}/patch_msc_core.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${msc_upstream}" "${msc_patch}")
    execute_process(COMMAND "${patch_python}" "${msc_patch}" "${msc_upstream}" "${msc_patched}"
                    RESULT_VARIABLE msc_patch_result)
    if(NOT msc_patch_result EQUAL 0)
        message(FATAL_ERROR "Pinned TinyUSB MSC status patch failed")
    endif()
    get_target_property(msc_core_sources ${tusb_lib} SOURCES)
    set(msc_replaced 0)
    set(msc_retained_sources "")
    foreach(msc_source IN LISTS msc_core_sources)
        if(msc_source MATCHES "(^|/)src/class/msc/msc_device\\.c$")
            math(EXPR msc_replaced "${msc_replaced} + 1")
        else()
            list(APPEND msc_retained_sources "${msc_source}")
        endif()
    endforeach()
    if(NOT msc_replaced EQUAL 1)
        message(FATAL_ERROR "Expected exactly one pinned TinyUSB MSC compile unit")
    endif()
    set_property(TARGET ${tusb_lib} PROPERTY SOURCES "${msc_retained_sources};${msc_patched}")
endif()
