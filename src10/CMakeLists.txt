# ImageWalker 1.0: a thumbnail browser and
# image viewer using the Windows SDK and libfiles.

set(IW10_SOURCES
    main.cpp
    test.cpp
    MainFrm.cpp
    ImageView.cpp
    ItemsView.cpp
    Jobs.cpp
    Thumb.cpp
    Dib.cpp
    DibDC.cpp
    DibExtra.cpp
    DibMemory.cpp
    LoadAny.cpp
    SdkUi.cpp
    StdAfx.cpp
)

list(TRANSFORM IW10_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_executable(imagewalker10 WIN32 ${IW10_SOURCES} ImageWalker.rc)

target_include_directories(imagewalker10 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/core")

target_compile_definitions(imagewalker10 PRIVATE
    _MBCS STRICT _CRT_SECURE_NO_WARNINGS
    WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_IE=0x0A00)
target_compile_options(imagewalker10 PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/permissive-;/Zc:__cplusplus;/utf-8;/W4>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/Zi>")

target_precompile_headers(imagewalker10 PRIVATE StdAfx.h)

target_link_libraries(imagewalker10 PRIVATE
    libfiles::libfiles
    advapi32 comctl32 dbghelp gdi32 htmlhelp ole32 shell32 shlwapi user32 uuid version windowscodecs
)

# The app embeds its own manifest as resource 1.
target_link_options(imagewalker10 PRIVATE /MANIFEST:NO /SUBSYSTEM:WINDOWS,6.01
    $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)

add_test(NAME imagewalker10-sdk-boundary COMMAND "${CMAKE_COMMAND}"
    "-DIW10_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" -P
    "${PROJECT_SOURCE_DIR}/tests/imaging/check-iw10-sdk.cmake")
set_tests_properties(imagewalker10-sdk-boundary PROPERTIES LABELS "iw10;headless" TIMEOUT 30)
