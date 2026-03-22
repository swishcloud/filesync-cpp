# CMakeList.txt : CMake project for filesync, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
set(CORE_TARGET_NAME filesync-core)
set(TCP_DIR "$ENV{INSTALL_PREFIX}/lib/cmake/TCP")
set(COMMON_DIR "$ENV{INSTALL_PREFIX}/lib/cmake/COMMON")
message(STATUS TCP_DIR::::${TCP_DIR})
find_package(TCP REQUIRED)
find_package(COMMON REQUIRED)
if(CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
if(${CMAKE_SYSTEM_NAME} MATCHES "iOS")
set(DEPENDENCY_SUFFIX iphoneos-arm64)
else()
set(DEPENDENCY_SUFFIX  macosx-x86_64)
endif()
set(Boost_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/Boost-1.90.0")
set(boost_headers_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_headers-1.90.0")
set(boost_date_time_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_date_time-1.90.0")
set(boost_locale_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_locale-1.90.0")
set(boost_atomic_DIR  "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_atomic-1.90.0")
set(boost_charconv_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_charconv-1.90.0")
set(boost_chrono_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_chrono-1.90.0")
set(boost_container_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_container-1.90.0")
set(boost_exception_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_exception-1.90.0")
set(boost_thread_DIR "$ENV{BOOST_ROOT}/${DEPENDENCY_SUFFIX}/lib/cmake/boost_thread-1.90.0")
set(OPENSSL_ROOT_DIR $ENV{OPENSSL_DIR}/${DEPENDENCY_SUFFIX})
set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.a)
set(OPENSSL_SSL_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libssl.a)
set(OPENSSL_INCLUDE_DIR ${OPENSSL_ROOT_DIR}/include)
endif()
find_package(OpenSSL REQUIRED)
find_package(Boost 1.75.0 REQUIRED COMPONENTS date_time locale thread)
find_package(nlohmann_json 3.2.0 REQUIRED)
add_compile_definitions("WIN32_LEAN_AND_MEAN")
# Add source to this project's executable.
add_library(common::common ALIAS COMMON::COMMON)
add_library(${CORE_TARGET_NAME} "filesync.cpp" "db_manager.cpp" "ChangeCommitter.cpp" "cfg.cpp" "internal.cpp" "server.cpp" "cmd.cpp" "cmd/LoginCMD.cpp" "cmd/SyncCMD.cpp" "file.cpp")
if (MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    set(CMAKE_EXE_LINKER_FLAGS "")
endif()
target_link_libraries(${CORE_TARGET_NAME} PRIVATE COMMON::COMMON ThirdPartyLibrary::TCP Boost::headers Boost::boost Boost::date_time Boost::locale Boost::thread
OpenSSL::SSL
OpenSSL::Crypto
nlohmann_json::nlohmann_json
sqlite)
if (UNIX AND NOT APPLE)
target_link_libraries(${CORE_TARGET_NAME} PRIVATE -pthread -ldl -lstdc++fs)
endif()
message(STATUS "Boost_INCLUDE_DIRS=${Boost_INCLUDE_DIRS}")
target_include_directories(${CORE_TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> ${Boost_INCLUDE_DIRS} "${OPENSSL_INCLUDE_DIR}" ${OPENSSL_INCLUDE_DIR})
add_custom_target(ThidPartyBinaries
COMMENT ("copying binaries")
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libssl-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${OPENSSL_BIN_DIR}"/libcrypto-3.dll" ${CMAKE_CURRENT_BINARY_DIR}
#COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}"/lib/sqlite3.dll" ${CMAKE_CURRENT_BINARY_DIR}
COMMENT ("copied filesyncinaries.")
)
add_dependencies(${CORE_TARGET_NAME} ThidPartyBinaries)

set(EXPORT_TARGET_NAME ${CORE_TARGET_NAME}Targets)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
# ---- Install the library + headers ----
install(TARGETS ${CORE_TARGET_NAME}
  EXPORT ${EXPORT_TARGET_NAME}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(TARGETS sqlite
  EXPORT ${EXPORT_TARGET_NAME}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/filesync)

# ---- Exported targets file ----
install(EXPORT ${EXPORT_TARGET_NAME}
  FILE ${EXPORT_TARGET_NAME}.cmake
  NAMESPACE FILESYNC::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${CORE_TARGET_NAME}
)

# ---- Config + version files ----
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${CORE_TARGET_NAME}Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${CORE_TARGET_NAME}Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${CORE_TARGET_NAME}
)
message(STATUS PROJECT_VERSION::::::::::${PROJECT_VERSION})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/${CORE_TARGET_NAME}ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${CORE_TARGET_NAME}Config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/${CORE_TARGET_NAME}ConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${CORE_TARGET_NAME}
)