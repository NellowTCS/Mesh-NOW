cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME "mesh-now")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/../Build")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(${PROJECT_NAME})
