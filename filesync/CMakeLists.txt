cmake_minimum_required (VERSION 3.8)
set(FILESYNC_TARGET_NAME filesync)
add_executable(${FILESYNC_TARGET_NAME} filesync.cpp)
target_link_libraries(${FILESYNC_TARGET_NAME} PRIVATE filesync-core)