# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)
project(filesync_solution VERSION 1.0.0)

message(STATUS PROJECT_VERSION::::::::::${PROJECT_VERSION})
add_compile_options(-D__USE_W32_SOCKETS -D_WIN32_WINNT=0x0601 -I/opt/boost)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_PREFIX_PATH "/opt/openssl")
#set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Include sub-projects.
add_subdirectory("sqlite")
add_subdirectory ("core")
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "iOS")
add_subdirectory ("test")
add_subdirectory ("filesync")
endif()