cmake_minimum_required(VERSION 3.16)

# Gedeelde components map één niveau omhoog
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(patnic_wekker_frontend)

