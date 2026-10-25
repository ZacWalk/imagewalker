add_library(libiw STATIC)
add_library(iw::libiw ALIAS libiw)
include(legacy.cmake)
target_sources(libiw PRIVATE ${LIBIW_LEGACY_SOURCES})
set(LIBIW_MODERN_SOURCES
    src/Pixel.cpp src/Cpu.cpp src/Diagnostics.cpp src/PixelOps.cpp src/PixelOpsAvx2.cpp
    src/ImageResampler.cpp src/ImageEditor.cpp src/EditGeometry.cpp
    src/WebPCodec.cpp src/JpegMetadata.cpp src/WicCodec.cpp)
target_sources(libiw PRIVATE ${LIBIW_MODERN_SOURCES})
target_include_directories(libiw PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/include")
target_include_directories(libiw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy"
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/iw/imaging")
target_compile_features(libiw PUBLIC cxx_std_20)
target_compile_definitions(libiw PRIVATE UNICODE _UNICODE STRICT
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# RelWithDebInfo, the preset's release configuration, lowers /O2's /Ob2 to /Ob1,
# and /Ob also limits LTCG's cross-module inlining of the line converters and
# pixel loops. Replaced rather than appended, which would report D9025.
string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
target_compile_options(libiw PRIVATE /EHs /W4 /Zc:__cplusplus /utf-8 /MP
    $<$<NOT:$<CONFIG:Debug>>:/Zi>)
set_target_properties(libiw PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set_source_files_properties(${LIBIW_LEGACY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "/permissive;/wd4996;/wd4100;/wd4239")
set_source_files_properties(${LIBIW_MODERN_SOURCES} PROPERTIES COMPILE_OPTIONS /permissive-)
set_property(SOURCE src/PixelOpsAvx2.cpp APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
target_link_libraries(libiw PRIVATE
    iw::jpeg iw::png iw::tiff iw::gif iw::zlib iw::webp libfiles::libfiles
    gdi32 user32 ole32 oleaut32 shlwapi shell32 windowscodecs)

add_executable(libiw_tests tests/test.cpp tests/legacy.cpp)
target_link_libraries(libiw_tests PRIVATE iw::libiw iw::jpeg iw::png iw::gif iw::tiff)
target_include_directories(libiw_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/iw/imaging")
target_compile_definitions(libiw_tests PRIVATE UNICODE _UNICODE STRICT _CRT_SECURE_NO_WARNINGS)
target_compile_options(libiw_tests PRIVATE /W4 /utf-8)
set_source_files_properties(tests/test.cpp PROPERTIES COMPILE_OPTIONS "/EHsc;/permissive-")
set_source_files_properties(tests/legacy.cpp PROPERTIES
    COMPILE_OPTIONS "/EHs;/permissive;/wd4996;/wd4100;/wd4239")
add_test(NAME libiw COMMAND libiw_tests)
set_tests_properties(libiw PROPERTIES TIMEOUT 120
    LABELS "libiw;iw10;iw20;iw22;iw23;iw30")
add_test(NAME libiw-boundary COMMAND "${CMAKE_COMMAND}"
    "-DIW_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-boundary.cmake")
set_tests_properties(libiw-boundary PROPERTIES TIMEOUT 120
    LABELS "libiw;iw10;iw20;iw22;iw23;iw30")
