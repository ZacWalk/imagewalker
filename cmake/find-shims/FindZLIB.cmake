# Shim. libpng/libtiff call find_package(ZLIB); point them at the zlib we built
# with FetchContent instead of letting them look for an installed copy.
if(NOT TARGET ZLIB::ZLIB)
    message(FATAL_ERROR "FindZLIB shim used before third-party/zlib was configured")
endif()

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_VERSION "${IW_ZLIB_VERSION}")
set(ZLIB_VERSION_STRING "${IW_ZLIB_VERSION}")
