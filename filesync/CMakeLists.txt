# CMakeList.txt : CMake project for filesync, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
find_package(openssl REQUIRED)
set(CMAKE_PREFIX_PATH "C:/workspace/repos/json/out/install/x64-Debug (default)")
find_package(nlohmann_json 3.2.0 REQUIRED)
set(BOOST_LIB_DIR "/opt/boost/stage/lib")
# Add source to this project's executable.
add_executable (filesync "filesync.cpp" "include/filesync.h" "db_manager.cpp"  "include/db_manager.h" "http.cpp" "include/http.h" "tcp.cpp" "include/tcp.h" "ChangeCommitter.cpp" "cfg.cpp" "internal.cpp")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
message(------------BOOST DIRECTORY:${BOOST_LIB_DIR})
# TODO: Add tests and install targets if needed.
target_link_libraries(filesync PRIVATE ${COMMON_LIBRARY} /workspace/c++/filesync/filesync/lib/sqlite3.a
"${BOOST_LIB_DIR}/libboost_date_time.a"
"${BOOST_LIB_DIR}/libboost_locale.a"
"${BOOST_LIB_DIR}/libboost_regex.a"
"${BOOST_LIB_DIR}/libboost_thread.a"
${OPENSLL_libssl}
${OPENSLL_libcrypto}
nlohmann_json::nlohmann_json
-pthread
-ldl
-lstdc++fs)
file(COPY "lib\\sqlite3.dll" DESTINATION .)
file(COPY "..\\.cache\\token" DESTINATION .)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}\\include" ${COMMON_INCLUDE} "/opt/boost" ${OPENSSL_INCLUDE_DIR}/include)
add_custom_target(ThidPartyBinaries
COMMENT ("copying binaries")
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libssl-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libcrypto-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}"/lib/sqlite3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMENT ("copied binaries.")
)
add_dependencies(filesync ThidPartyBinaries)