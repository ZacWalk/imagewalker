# ImageWalker 3.0 -> imagewalker30.exe
#
# Not a port: 3.0 is written from scratch against the modern SDK, so it does not
# take iw::flags -- that interface exists to keep the 2005-era ATL/WTL trees
# compiling under /permissive and suppresses warnings this code does not need.
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
    PixelOps.cpp
    PixelOpsAvx2.cpp
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
    WebPCodec.cpp
)
list(TRANSFORM IW30_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_executable(imagewalker30 WIN32 ${IW30_SOURCES} app.rc)

target_include_directories(imagewalker30 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/core")

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

# The blitter has one AVX2 path; everything else has to run without it.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/PixelOpsAvx2.cpp"
    PROPERTIES COMPILE_OPTIONS /arch:AVX2)

target_link_libraries(imagewalker30 PRIVATE comctl32 windowscodecs ole32 shlwapi htmlhelp
    mfuuid bcrypt iw::webp)

# Optimised builds keep their PDBs.
target_link_options(imagewalker30 PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)

# The app embeds its own manifest as resource 1.
target_link_options(imagewalker30 PRIVATE /MANIFEST:NO)
