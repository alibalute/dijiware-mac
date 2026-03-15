# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# IDF_PATH must point to a valid ESP-IDF install (set by export.sh / export.bat)
if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set. Source the ESP-IDF export script first, e.g.: source $HOME/.espressif/esp-idf/export.sh")
endif()
set(idf_project_cmake "$ENV{IDF_PATH}/tools/cmake/project.cmake")
if(NOT EXISTS "${idf_project_cmake}")
  message(FATAL_ERROR "ESP-IDF not found at IDF_PATH=$ENV{IDF_PATH}\n  Missing: ${idf_project_cmake}\n  Install ESP-IDF or set IDF_PATH to your ESP-IDF root (e.g. .../v5.0.1/esp-idf).")
endif()
include("${idf_project_cmake}")
project(etar)
