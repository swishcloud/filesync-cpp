# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
project(filesync_solution)
add_compile_options(-D__USE_W32_SOCKETS -D_WIN32_WINNT=0x0601 -I/opt/boost)
cmake_minimum_required (VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
set(CMAKE_PREFIX_PATH "/opt/openssl")
set(BOOST_LIB_DIR "/opt/boost/stage/lib")
set(BOOST_INCLUDE_DIR "/opt/boost")
else()
set(CMAKE_PREFIX_PATH "C:/workspace/repos/openssl")
set(BOOST_LIB_DIR "C:/Program Files (x86)/b2/lib")
set(BOOST_INCLUDE_DIR "C:/Program Files (x86)/b2/include/boost-1_74")
endif()
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
message("CMAKE_MODULE_PATH:${CMAKE_MODULE_PATH}")
# Include sub-projects.
add_subdirectory ("filesync")