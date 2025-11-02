cmake_minimum_required(VERSION 3.16)

# Set project name  
set(PROJECT_NAME "mesh-now")

# Include ESP-IDF CMake build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})