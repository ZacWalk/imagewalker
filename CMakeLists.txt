cmake_minimum_required(VERSION 3.28)

project(imagewalker-all
        DESCRIPTION "ImageWalker, reconstructed: one 64-bit WTL executable per historical version"
        LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "ImageWalker builds with MSVC on Windows only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "x64 only -- configure with one of the x64 presets.")
endif()

find_path(IW_ATL_INCLUDE_DIR atlbase.h HINTS ENV INCLUDE)
if(NOT IW_ATL_INCLUDE_DIR)
    message(FATAL_ERROR "ATL is required. Add Microsoft.VisualStudio.Component.VC.ATL to the selected Visual Studio C++ installation; see docs/build.md.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
enable_testing()

# The Store build of PowerShell lives under a version-stamped MSIX directory,
# so a cached path stops existing as soon as pwsh updates. Drop a stale entry
# and prefer the stable per-machine install over that alias.
if(IW_PWSH_EXECUTABLE AND NOT EXISTS "${IW_PWSH_EXECUTABLE}")
    unset(IW_PWSH_EXECUTABLE CACHE)
endif()
find_program(IW_PWSH_EXECUTABLE NAMES pwsh
    HINTS "$ENV{ProgramFiles}/PowerShell/7" "$ENV{ProgramW6432}/PowerShell/7" REQUIRED)
add_test(NAME dd-contract COMMAND "${IW_PWSH_EXECUTABLE}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-dd.ps1")
set_tests_properties(dd-contract PROPERTIES TIMEOUT 120)

# Static CRT: these exes are meant to be dropped in a folder and run.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Safe to share between the debug and release build trees: the debug exes carry
# a 'd' suffix, so the two configurations never write the same file.
set(IW_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/exe" CACHE PATH
    "Directory the imagewalker<ver>[d].exe files are written to")

# Keep dependency outputs in each preset's own build tree.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "" FORCE)

# Settings shared by the WTL app targets (1.0 through 2.3). Deliberately NOT
# applied to third-party, nor to 3.0, which is modern code held to strict
# conformance.
add_library(iw_flags INTERFACE)
add_library(iw::flags ALIAS iw_flags)
target_compile_features(iw_flags INTERFACE cxx_std_20)
# /std:c++20 turns /permissive- on. These sources are 2005-era ATL/WTL and rely
# throughout on the old dependent-name lookup, so turn it back off rather than
# rewrite ~1100 inherited-member references.
# Dependency recipes can replace cached CMAKE_CXX_FLAGS; keep destructor
# unwinding explicit rather than relying on the compiler's default flags.
target_compile_options(iw_flags INTERFACE
    /EHsc /permissive /Zc:__cplusplus /utf-8 /W4 /MP
    /wd4996  # the 1998-era CRT calls are being ported, not rewritten
    # /W4 additions that are inherent to this codebase rather than defects:
    /wd4100  # unreferenced formal parameter - every WTL message handler takes
             # (UINT, WPARAM, LPARAM, BOOL&) whether or not it uses them. 3897 sites.
    /wd4239  # non-const reference bound to a temporary - the ATL idiom that
             # /permissive exists to allow here. Turning it off would mean
             # /permissive-, which costs ~1100 errors.
    /wd4131  # old-style declarator - only the vendored BSD regex C sources.
)
# Optimised builds keep their PDBs: the crash reporter is useless without them.
# Not $<CONFIG:Release> -- no preset produces a config by that name, so those
# generator expressions never fired and /OPT:REF was never applied.
target_compile_options(iw_flags INTERFACE $<$<NOT:$<CONFIG:Debug>>:/Zi>)
target_link_options(iw_flags INTERFACE $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)
# Character set is per-app: ArtMate is MBCS; all three 2.x ports are Unicode.
target_compile_definitions(iw_flags INTERFACE
    STRICT
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 _WIN32_IE=0x0A00
)
target_include_directories(iw_flags INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(iw_flags INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/core")
target_include_directories(iw_flags INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/wtl")
target_link_libraries(iw_flags INTERFACE
    comctl32 shlwapi gdi32 user32 ole32 oleaut32 uuid advapi32 shell32 comdlg32 winspool htmlhelp
    dbghelp version  # iw/crashreport.h
)

# Keeps the ~100 third-party targets out of the way in the VS solution tree.
set(CMAKE_FOLDER "third-party")
add_subdirectory(third-party)

unset(CMAKE_FOLDER)

# One directory per release. 3.0 brings its own compiler settings; the rest
# share iw::flags.
set(IW_VERSIONS 10 20 22 23 30)

foreach(ver IN LISTS IW_VERSIONS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src${ver}/CMakeLists.txt")
        add_subdirectory(src${ver})
    endif()
endforeach()

# One place decides what lands in exe/, so the naming cannot drift per app:
# imagewalker<ver>.exe for release, imagewalker<ver>d.exe for debug. The apps
# derive their .ini and .log names from the exe, so a debug and a release build
# in the same directory keep separate settings and separate logs.
foreach(ver IN LISTS IW_VERSIONS)
    if(TARGET imagewalker${ver})
        set_target_properties(imagewalker${ver} PROPERTIES
            OUTPUT_NAME "imagewalker${ver}"
            DEBUG_POSTFIX d
            RUNTIME_OUTPUT_DIRECTORY "${IW_OUTPUT_DIR}")
        add_test(NAME imagewalker${ver} COMMAND $<TARGET_FILE:imagewalker${ver}> /test)
        set_tests_properties(imagewalker${ver} PROPERTIES
            WORKING_DIRECTORY "${IW_OUTPUT_DIR}"
            TIMEOUT 120
            RUN_SERIAL TRUE
            LABELS "iw${ver}")
    endif()
endforeach()

# Visual Studio generator: open build/vs/imagewalker-all.slnx and just hit F5.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT imagewalker30)

    foreach(ver IN LISTS IW_VERSIONS)
        if(TARGET imagewalker${ver})
            set_target_properties(imagewalker${ver} PROPERTIES
                VS_DEBUGGER_WORKING_DIRECTORY "${IW_OUTPUT_DIR}")

            # Multi-config generators append the config name to
            # RUNTIME_OUTPUT_DIRECTORY; the per-config property does not, which
            # keeps the exes beside the ones the Ninja trees produce.
            foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
                string(TOUPPER "${cfg}" cfg_upper)
                set_target_properties(imagewalker${ver} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${IW_OUTPUT_DIR}")
            endforeach()
        endif()
    endforeach()
endif()

# --- help file --------------------------------------------------------------
# All five applications open ImageWalker.chm beside their executable.
# The 3.0 topics are independent of the historical topic set.
# hhc.exe from HTML Help Workshop was the historical compiler. Microsoft
# withdrew it, it only ever existed as a 32-bit binary, and the surviving copies
# come from mirrors of unclear provenance, so tools/chmbuild.py writes the CHM
# container directly. Windows' own hh.exe reads the result -- see the notes at
# the top of that script.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

set(IW_HELP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Help")
set(IW_HELP_PROJECT "${IW_HELP_DIR}/imagewalker.hhp")
set(IW_HELP_OUTPUT "${IW_OUTPUT_DIR}/ImageWalker.chm")

if(Python3_Interpreter_FOUND AND EXISTS "${IW_HELP_PROJECT}")
    file(GLOB IW_HELP_SOURCES CONFIGURE_DEPENDS
        "${IW_HELP_DIR}/*.html" "${IW_HELP_DIR}/*.hhc" "${IW_HELP_DIR}/*.hhk"
        "${IW_HELP_DIR}/*.css"  "${IW_HELP_DIR}/images/*")

    # Both build trees write this one path. That is normally a trap - each tree
    # believes its own output is current while the other overwrote it - but the
    # help file has no per-configuration content, so the worst case is a
    # redundant recompile of a file that comes out identical either way.
    add_custom_command(
        OUTPUT "${IW_HELP_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/chmbuild.py"
                "${IW_HELP_PROJECT}" --strict -o "${IW_HELP_OUTPUT}"
        # helpids.h is the [MAP] section: the context IDs the apps are compiled
        # against and the ones baked into the help file have to come from the
        # same edit, or F1 opens the wrong topic.
        DEPENDS "${IW_HELP_PROJECT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/chmbuild.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/iw/helpids.h"
                ${IW_HELP_SOURCES}
        COMMENT "Compiling ImageWalker.chm"
        VERBATIM)

    add_custom_target(iw_help ALL DEPENDS "${IW_HELP_OUTPUT}")
    set_target_properties(iw_help PROPERTIES FOLDER "help")
    foreach(ver IN LISTS IW_VERSIONS)
        if(TARGET imagewalker${ver})
            add_dependencies(imagewalker${ver} iw_help)
        endif()
    endforeach()
    add_custom_target(iw_check_help
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/checkhelp.py"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/chmbuild.py"
            "${IW_HELP_PROJECT}" --strict -o "${IW_HELP_OUTPUT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
    set_target_properties(iw_check_help PROPERTIES FOLDER "help")
else()
    # Not fatal: the apps run without a help file, they just cannot show it.
    message(STATUS "Help file not built (Python 3.9+ required); ImageWalker.chm will be missing")
endif()
