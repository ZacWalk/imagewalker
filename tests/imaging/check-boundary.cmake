# Ownership and direct-dependency checks for the retired engine target.
#
# This is intentionally a source check rather than a link check: it keeps a
# future compatibility wrapper from quietly recreating the old library facade.

if(NOT DEFINED IW_SOURCE_DIR)
    message(FATAL_ERROR "IW_SOURCE_DIR is required")
endif()

set(source "${IW_SOURCE_DIR}")
if(NOT IS_DIRECTORY "${source}/include/iw/imaging" OR
   NOT IS_DIRECTORY "${source}/include/iw/legacy")
    message(FATAL_ERROR "Shared imaging headers must live under include/iw/imaging and include/iw/legacy")
endif()

foreach(old_dir IN ITEMS "${source}/libiw" "${source}/libiw/include" "${source}/libiw/compat")
    file(GLOB_RECURSE old_headers "${old_dir}/*.h" "${old_dir}/*.hpp")
    if(old_headers)
        message(FATAL_ERROR "Retired libiw public/compatibility headers remain under ${old_dir}")
    endif()
endforeach()

file(GLOB_RECURSE shared_implementations
    "${source}/src/shared/*.cpp" "${source}/src/shared/*.c")
if(shared_implementations)
    message(FATAL_ERROR "Shared application code must be header-only: ${shared_implementations}")
endif()

file(READ "${source}/cmake/imagewalker-imaging.cmake" imaging_build)
if(imaging_build MATCHES "add_library\\([^\\)]*(OBJECT|STATIC)" OR
   imaging_build MATCHES "src/shared")
    message(FATAL_ERROR "Shared imaging must not compile an implementation or object library")
endif()

file(READ "${source}/CMakeLists.txt" root_build)
if(root_build MATCHES "iw::libiw")
    message(FATAL_ERROR "The root build still exposes iw::libiw")
endif()

foreach(version IN ITEMS 10 20 22 23 30)
    file(READ "${source}/src${version}/CMakeLists.txt" app_build)
    if(app_build MATCHES "iw::libiw" OR app_build MATCHES "libiw/(compat|include)" OR
       app_build MATCHES "src/shared|iw_modern_sources|iw_legacy_sources")
        message(FATAL_ERROR "ImageWalker ${version} has a private libiw dependency")
    endif()
endforeach()

file(GLOB_RECURSE modern_headers "${source}/include/iw/imaging/*.h")
foreach(path IN LISTS modern_headers)
    file(READ "${path}" text)
    if(text MATCHES "#include[ \t]*[<\"]iw/legacy/" OR
       text MATCHES "#include[ \t]*[<\"](atl|windows|wtl|objbase|intrin|immintrin)" OR
       text MATCHES "#include[^\r\n]*\\.cpp[>\"]")
        message(FATAL_ERROR "Modern public header leaks historical/platform types: ${path}")
    endif()
endforeach()

# A forwarding header in an application would make ownership drift again. An
# app adapter may include a shared header and then declare its own types; only
# reject a file whose body is just pragma-once plus one include.
file(GLOB_RECURSE app_headers
    "${source}/src10/*.h" "${source}/src20/*.h"
    "${source}/src22/*.h" "${source}/src23/*.h")
foreach(path IN LISTS app_headers)
    file(READ "${path}" text)
    string(REGEX REPLACE "[\r\n\t ]" "" compact "${text}")
    if(compact MATCHES "^#pragmaonce#include[<\"]iw/(imaging|legacy)/[^<\"]+[>\"][;]?$")
        message(FATAL_ERROR "Application forwarding header remains: ${path}")
    endif()
endforeach()

# IW30's active presentation boundary is libdesktop's ui/render layers. Retired
# Win32/GDI implementations must not remain in source (even behind #if 0),
# because they are routinely re-enabled by an accidental merge conflict.
foreach(path IN ITEMS
    "${source}/src30/Platform.cpp"
    "${source}/src30/PlatformChrome.cpp"
    "${source}/src30/PlatformDialog.cpp"
    "${source}/src30/PlatformShellTree.cpp")
    if(EXISTS "${path}")
        file(READ "${path}" text)
        if(text MATCHES "WinWindowFrame|WinDrawContext|Retired native frame/draw implementation" OR
           text MATCHES "Retired hidden HWND dispatcher" OR
           text MATCHES "Retained only as a historical reference")
            message(FATAL_ERROR "IW30 retains a retired private Win32 GUI implementation: ${path}")
        endif()
    endif()
endforeach()

foreach(codec_source IN ITEMS
    "${source}/include/iw/legacy/detail/FileBmp.h"
    "${source}/include/iw/legacy/detail/FileGif.h"
    "${source}/include/iw/legacy/detail/FilePng.h"
    "${source}/include/iw/legacy/detail/FileTiff.h"
    "${source}/include/iw/legacy/detail/FileJpeg.h")
    if(EXISTS "${codec_source}")
        file(READ "${codec_source}" text)
        if(text MATCHES "jpeg_(set_|start_|write_|finish_)|png_(create_|write_)|TIFF(Write|SetField|ClientOpen)|EGif|MakeMapObject")
            message(FATAL_ERROR "Native codec writer body remains in historical adapter: ${codec_source}")
        endif()
    endif()
endforeach()

message(STATUS "ImageWalker imaging ownership and direct-dependency boundaries verified")
