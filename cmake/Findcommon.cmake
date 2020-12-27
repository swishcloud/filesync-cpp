message("finding common...")
set(COMMON_LIB_NAME common)
find_library(COMMON_LIBRARY NAMES ${COMMON_LIB_NAME})
find_path(COMMON_INCLUDE NAMES common.h)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(common REQUIRED_VARS COMMON_LIBRARY COMMON_INCLUDE)