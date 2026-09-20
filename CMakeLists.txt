cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# project.cmake initializes this property before project() invokes dependency
# resolution. Use that selected IDF interpreter, which owns the manager API.
idf_build_get_property(scanner_idf_python PYTHON)
if(NOT scanner_idf_python)
    message(FATAL_ERROR "ESP-IDF did not select its Python interpreter")
endif()
execute_process(
    COMMAND "${scanner_idf_python}" "${CMAKE_CURRENT_LIST_DIR}/tools/rebase_dependencies_lock.py"
            --project-root "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE scanner_lock_result
)
if(NOT scanner_lock_result EQUAL 0)
    message(FATAL_ERROR "Cannot prepare the local esp_tinyusb dependency lock")
endif()
project(scanner_esp_gateway)
