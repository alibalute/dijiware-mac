# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Single source of truth for BLE/UI “firmware version” (see main.c sscanf on esp_app_desc_t::version).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
  string(STRIP "${PROJECT_VER}" PROJECT_VER)
endif()

# IDF_PATH must point to a valid ESP-IDF install (set by export.sh / export.bat)
if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set. Source the ESP-IDF export script first, e.g.: source $HOME/.espressif/esp-idf/export.sh")
endif()
set(idf_project_cmake "$ENV{IDF_PATH}/tools/cmake/project.cmake")
if(NOT EXISTS "${idf_project_cmake}")
  message(FATAL_ERROR "ESP-IDF not found at IDF_PATH=$ENV{IDF_PATH}\n  Missing: ${idf_project_cmake}\n  Install ESP-IDF or set IDF_PATH to your ESP-IDF root (e.g. .../v5.0.1/esp-idf).")
endif()
include("${idf_project_cmake}")
project(dijilele)

# After every build: copy the flashable .bin into firmware/ with an OTA-friendly name
# (matches DijiApp: <slug>-<major>-<minor>-<patch>.bin, e.g. dijilele-m-1-0-4.bin).
# Product slug comes from DIJILELE_PRODUCT_ID (set by build-4mb.sh / build-16mb.sh).
if(DEFINED PROJECT_VER AND NOT PROJECT_VER STREQUAL "")
  set(DIJILELE_OTA_SLUG "unknown")
  if(DEFINED ENV{DIJILELE_PRODUCT_ID} AND NOT "$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "")
    if("$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "1")
      set(DIJILELE_OTA_SLUG "dijilele-m")
    elseif("$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "2")
      set(DIJILELE_OTA_SLUG "dijilele-s")
    endif()
  endif()
  string(REPLACE "." "-" PROJECT_VER_OTA "${PROJECT_VER}")
  set(DIJILELE_OTA_BIN "${DIJILELE_OTA_SLUG}-${PROJECT_VER_OTA}.bin")
  add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/firmware"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
            "${CMAKE_SOURCE_DIR}/firmware/${DIJILELE_OTA_BIN}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/update-ota-manifest.sh"
    COMMENT "Copy -> firmware/${DIJILELE_OTA_BIN}; update OTA manifest"
    VERBATIM
  )
endif()
