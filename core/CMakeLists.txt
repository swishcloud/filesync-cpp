# CMakeList.txt : CMake project for filesync, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
set(CORE_TARGET_NAME filesync-core)
find_package(TCP REQUIRED)
find_package(COMMON REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time locale regex thread program_options)
find_package(nlohmann_json 3.2.0 REQUIRED)
add_compile_definitions("WIN32_LEAN_AND_MEAN")
# Add source to this project's executable.
add_library(common::common ALIAS COMMON::COMMON)
add_library(${CORE_TARGET_NAME} "filesync.cpp" "db_manager.cpp" "ChangeCommitter.cpp" "cfg.cpp" "internal.cpp" "server.cpp" "cmd.cpp")
if (MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    set(CMAKE_EXE_LINKER_FLAGS "")
endif()
target_link_libraries(${CORE_TARGET_NAME} PRIVATE COMMON::COMMON ThirdPartyLibrary::TCP Boost::date_time Boost::locale Boost::regex Boost::thread Boost::program_options
OpenSSL::SSL
OpenSSL::Crypto
nlohmann_json::nlohmann_json
sqlite)
if (UNIX)
target_link_libraries(${CORE_TARGET_NAME} PRIVATE -pthread -ldl -lstdc++fs)
endif()
target_include_directories(${CORE_TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> "/opt/boost" ${OPENSSL_INCLUDE_DIR}/include)
add_custom_target(ThidPartyBinaries
COMMENT ("copying binaries")
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libssl-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libcrypto-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}"/lib/sqlite3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMENT ("copied filesyncinaries.")
)
add_dependencies(${CORE_TARGET_NAME} ThidPartyBinaries)