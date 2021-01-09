message("finding OpenSSL...")
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
find_library(OPENSLL_libssl NAMES libssl.a)
find_library(OPENSLL_libcrypto NAMES libcrypto.a)
find_path(OPENSSL_BIN_DIR NAMES libcrypto.a PATH_SUFFIXES lib)
else()  
find_library(OPENSLL_libssl NAMES libssl)
find_library(OPENSLL_libcrypto NAMES libcrypto)
find_path(OPENSSL_BIN_DIR NAMES libcrypto.lib)
endif()
find_path(OPENSSL_INCLUDE_DIR NAMES include/openssl)
message(OPENSSL_INCLUDE_DIR> ${OPENSSL_INCLUDE_DIR})
message(OPENSLL_libssl>${OPENSLL_libssl})
message(OPENSLL_libcrypto>${OPENSLL_libcrypto})
message(OPENSSL_BIN_DIR>${OPENSSL_BIN_DIR})
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(openssl REQUIRED_VARS OPENSLL_libssl OPENSLL_libcrypto OPENSSL_INCLUDE_DIR OPENSSL_BIN_DIR)