cmake_minimum_required (VERSION 3.8)
set(TEST_TARGET_NAME filesync_test)
add_executable(${TEST_TARGET_NAME} test.cpp file_repository_test.cpp)
target_link_libraries(${TEST_TARGET_NAME} filesync-core)