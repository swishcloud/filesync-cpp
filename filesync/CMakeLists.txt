# CMakeList.txt : CMake project for filesync, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
find_package(TCP REQUIRED)
find_package(common REQUIRED)
find_package(openssl REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time locale regex thread)
set(CMAKE_PREFIX_PATH "C:/workspace/repos/json/out/install/x64-Debug (default)")
find_package(nlohmann_json 3.2.0 REQUIRED)
# Add source to this project's executable.
add_executable (filesync "filesync.cpp" "db_manager.cpp" "http.cpp" "tcp.cpp" "ChangeCommitter.cpp" "cfg.cpp" "internal.cpp" "server.cpp")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
message(------------BOOST DIRECTORY:${BOOST_LIB_DIR})
# TODO: Add tests and install targets if needed.
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
message("configuring BOOST lib list for linux...")
set(BOOST_LIB
"${BOOST_LIB_DIR}/libboost_locale.a"
"${BOOST_LIB_DIR}/libboost_regex.a"
"${BOOST_LIB_DIR}/libboost_thread.a")
set(LINUX_LIBS
-pthread
-ldl
-lstdc++fs
-D__USE_W32_SOCKETS)
else()
message("configuring BOOST lib list for windows...")
set(BOOST_LIB
"${BOOST_LIB_DIR}/libboost_date_time-vc142-mt-gd-x64-1_74.lib"
"${BOOST_LIB_DIR}/libboost_locale-vc142-mt-gd-x64-1_74.lib"
"${BOOST_LIB_DIR}/libboost_regex-vc142-mt-gd-x64-1_74.lib"
"${BOOST_LIB_DIR}/libboost_thread-vc142-mt-gd-x64-1_74.lib")
endif()
message(BOOST_LIB>${BOOST_LIB})
target_link_libraries(filesync PRIVATE ${COMMON_LIBRARY}
${OPENSLL_libssl}
${OPENSLL_libcrypto}
nlohmann_json::nlohmann_json
common::common
ThirdPartyLibrary::TCP
Boost::date_time Boost::locale Boost::regex Boost::thread
${LINUX_LIBS}
${CMAKE_CURRENT_SOURCE_DIR}/lib/sqlite3-debian.a)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include" ${COMMON_INCLUDE} ${BOOST_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR}/include)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
add_custom_target(ThidPartyBinaries
COMMENT ("copying binaries")
COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libssl-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libcrypto-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}"/lib/sqlite3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMENT ("copied binaries.")
)
add_dependencies(filesync  )
endif()