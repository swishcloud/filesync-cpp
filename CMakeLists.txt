# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
project(filesync_solution)
add_compile_options(-D__USE_W32_SOCKETS -D_WIN32_WINNT=0x0601 -I/opt/boost)
cmake_minimum_required (VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_PREFIX_PATH "/opt/openssl")
project(filesync_solution)
#set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
project ("filesync")

# Include sub-projects.
add_subdirectory ("core")
add_subdirectory("sqlite")
add_subdirectory ("filesync")
add_subdirectory ("test")
