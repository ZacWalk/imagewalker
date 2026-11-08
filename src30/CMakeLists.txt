# ImageWalker 3.0 -> imagewalker30.exe
#
# ImageWalker 3.0 uses the shared ui frame, render context and platform services.
# The historical ATL/WTL releases keep their own build targets and adapters.
# Its own topic set is included in the portable ImageWalker.chm.

set(IW30_SOURCES
    CanvasRenderer.cpp
    ControlPanel.cpp
    DiagnosticLog.cpp
    EditGeometry.cpp
    Files.cpp
    Format.cpp
    ImageCanvas.cpp
    ImageEditor.cpp
    ImageResampler.cpp
    Main.cpp
    MainWindow.cpp
    MainWindowFiles.cpp
    MainWindowTasks.cpp
    Media.cpp
    MediaRuntime.cpp
    ModeBar.cpp
    Paths.cpp
    Platform.cpp
    PlatformChrome.cpp
    PlatformDialog.cpp
    PlatformHelp.cpp
    PlatformMedia.cpp
    PlatformShellTree.cpp
    TaskConvert.cpp
    TaskEdit.cpp
    TaskMedia.cpp
    TaskRename.cpp
    TaskRunner.cpp
    TaskSync.cpp
    TaskView.cpp
    Tests.cpp
    Tools.cpp
    Undo.cpp
)
list(TRANSFORM IW30_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBDESKTOP
   AND EXISTS "${CMAKE_SOURCE_DIR}/../libdesktop/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBDESKTOP "${CMAKE_SOURCE_DIR}/../libdesktop" CACHE PATH
        "A libdesktop checkout to build instead of the published revision")
endif()
include(FetchContent)
FetchContent_Declare(libdesktop
    GIT_REPOSITORY https://github.com/ZacWalk/libdesktop.git
    GIT_TAG        04109ee82a500db38d439fa281bbc3f43124adfe
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libdesktop)

libdesktop_add_app(imagewalker30
    SOURCES ${IW30_SOURCES} app.rc
    EMBED
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/ToolbarSymbols.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/audio.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/document.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/folder.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/other.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/photo.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/status-symbols.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/FileTypes/video.bmp"
    DESCRIPTION "ImageWalker 3.0"
    VERSION "3.0.0.0"
    OUTPUT_NAME imagewalker30)

target_include_directories(imagewalker30 PRIVATE
    "${CMAKE_SOURCE_DIR}/include")

target_compile_features(imagewalker30 PRIVATE cxx_std_20)
target_compile_definitions(imagewalker30 PRIVATE UNICODE _UNICODE STRICT
                                                 WINVER=0x0A00 _WIN32_WINNT=0x0A00)
# /EHsc is stated rather than inherited: a vendored third-party project FORCEs
# CMAKE_CXX_FLAGS into the cache, which drops CMake's own default. Without it a
# throw does not destroy the automatic objects it passes, and the per-row
# handlers in TaskRunner would leak everything between the throw and the catch.
target_compile_options(imagewalker30 PRIVATE
    /EHsc /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /MP
    $<$<NOT:$<CONFIG:Debug>>:/Zi /guard:cf>)

target_link_libraries(imagewalker30 PRIVATE libdesktop::ui comctl32 windowscodecs ole32 shlwapi htmlhelp
    mfuuid bcrypt
    iw_imaging
    iw::jpeg iw::png iw::tiff iw::gif iw::zlib
    libfiles::libfiles)

# Optimised builds keep their PDBs.
target_link_options(imagewalker30 PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)
