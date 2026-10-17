# Shim. libtiff calls find_package(JPEG); point it at the libjpeg-turbo we built
# with FetchContent instead of letting it look for an installed copy.
if(NOT TARGET JPEG::JPEG)
    message(FATAL_ERROR "FindJPEG shim used before third-party/libjpeg-turbo was configured")
endif()

set(JPEG_FOUND TRUE)
set(JPEG_INCLUDE_DIR "${libjpegturbo_SOURCE_DIR}/src" "${libjpegturbo_BINARY_DIR}")
set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
set(JPEG_LIBRARY JPEG::JPEG)
set(JPEG_LIBRARIES JPEG::JPEG)
set(JPEG_VERSION "${IW_JPEG_VERSION}")
