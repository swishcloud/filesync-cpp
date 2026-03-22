cmake_minimum_required (VERSION 3.8)
set(FILESYNC_TARGET_NAME filesync)
add_executable(${FILESYNC_TARGET_NAME} filesync.cpp)
target_link_libraries(${FILESYNC_TARGET_NAME} PRIVATE filesync-core)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
set_target_properties(${FILESYNC_TARGET_NAME} PROPERTIES BUILD_RPATH "\$ORIGIN/dependencies" )
endif()