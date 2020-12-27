# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "C:\\Program Files (x86)\\common" "/opt/openssl")
project(filesync_solution)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
message("CMAKE_MODULE_PATH:${CMAKE_MODULE_PATH}")
find_package(common REQUIRED)
project ("filesync")

# Include sub-projects.
add_subdirectory ("filesync")