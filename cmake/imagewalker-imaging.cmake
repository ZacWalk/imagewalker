# Shared application headers contain inline adapters and application-owned algorithms.
# Compiled codecs and platform mechanisms belong to their dependency targets.
add_library(iw_imaging INTERFACE)
target_include_directories(iw_imaging INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(iw_imaging INTERFACE cxx_std_20)
target_link_libraries(iw_imaging INTERFACE
    libdesktop::core libdesktop::platform libfiles::libfiles)

add_library(iw_legacy INTERFACE)
target_include_directories(iw_legacy INTERFACE
    "${PROJECT_SOURCE_DIR}/include/iw/legacy")
target_link_libraries(iw_legacy INTERFACE iw_imaging
    iw::jpeg iw::png iw::tiff iw::gif iw::zlib
    gdi32 user32 ole32 oleaut32 shlwapi shell32 windowscodecs)

add_executable(imagewalker_imaging_tests
    "${PROJECT_SOURCE_DIR}/tests/imaging/test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/imaging/legacy.cpp")
target_link_libraries(imagewalker_imaging_tests PRIVATE iw_legacy)
target_compile_definitions(imagewalker_imaging_tests PRIVATE
    UNICODE _UNICODE STRICT _CRT_SECURE_NO_WARNINGS)
target_compile_options(imagewalker_imaging_tests PRIVATE /W4 /utf-8 /bigobj)
set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/imaging/test.cpp"
    PROPERTIES COMPILE_OPTIONS "/EHsc;/permissive-")
set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/imaging/legacy.cpp"
    PROPERTIES COMPILE_OPTIONS "/EHs;/permissive;/wd4996;/wd4100;/wd4239")
add_test(NAME imagewalker-imaging COMMAND imagewalker_imaging_tests)
set_tests_properties(imagewalker-imaging PROPERTIES TIMEOUT 120
    LABELS "imagewalker-imaging;iw10;iw20;iw22;iw23;iw30")

add_executable(imagewalker_headers_tests
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-main.cpp"
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-other.cpp"
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-legacy-a.cpp"
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-legacy-b.cpp")
target_link_libraries(imagewalker_headers_tests PRIVATE iw_legacy)
target_compile_definitions(imagewalker_headers_tests PRIVATE UNICODE _UNICODE)
target_compile_options(imagewalker_headers_tests PRIVATE /EHsc /permissive- /W4 /utf-8 /bigobj)
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-legacy-a.cpp"
    "${PROJECT_SOURCE_DIR}/tests/imaging/header-legacy-b.cpp"
    PROPERTIES COMPILE_OPTIONS "/permissive;/wd4996;/wd4100;/wd4239")
add_test(NAME imagewalker-headers COMMAND imagewalker_headers_tests)
set_tests_properties(imagewalker-headers PROPERTIES TIMEOUT 120
    LABELS "imagewalker-imaging;iw10;iw20;iw22;iw23;iw30")

add_test(NAME imagewalker-boundary COMMAND "${CMAKE_COMMAND}"
    "-DIW_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
    "${PROJECT_SOURCE_DIR}/tests/imaging/check-boundary.cmake")
set_tests_properties(imagewalker-boundary PROPERTIES TIMEOUT 120
    LABELS "imagewalker-imaging;iw10;iw20;iw22;iw23;iw30")
