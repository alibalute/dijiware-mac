cmake_minimum_required(VERSION 3.5)

# Set the components directory to include the parent project's components
# This allows the test app to test the actual project components
get_filename_component(PROJECT_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
list(APPEND EXTRA_COMPONENT_DIRS "${PROJECT_PATH}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_app)
