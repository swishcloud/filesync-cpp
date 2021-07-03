# CMakeList.txt : CMake project for filesync, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
find_package(TCP REQUIRED)
find_package(common REQUIRED)
find_package(openssl REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time locale regex thread program_options)
find_package(nlohmann_json 3.2.0 REQUIRED)
# Add source to this project's executable.
add_executable (filesync "filesync.cpp" "db_manager.cpp" "ChangeCommitter.cpp" "cfg.cpp" "internal.cpp" "server.cpp")
if (MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    set(CMAKE_EXE_LINKER_FLAGS "")
endif()
target_link_libraries(filesync PRIVATE common::common ThirdPartyLibrary::TCP Boost::date_time Boost::locale Boost::regex Boost::thread Boost::program_options
${OPENSLL_libssl}
${OPENSLL_libcrypto}
nlohmann_json::nlohmann_json
sqlite)
if (UNIX)
target_link_libraries(filesync PRIVATE -pthread -ldl -lstdc++fs)
endif()
include_directories("${CMAKE_CURRENT_SOURCE_DIR}\\include" ${COMMON_INCLUDE} "/opt/boost" ${OPENSSL_INCLUDE_DIR}/include)
add_custom_target(ThidPartyBinaries
COMMENT ("copying binaries")
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libssl-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libcrypto-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}"/lib/sqlite3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMENT ("copied binaries.")
)
add_dependencies(filesync ThidPartyBinaries)