# Modern Windows build constraints

Follow these rules when building ImageWalker 1.0 and the WTL releases (2.0,
2.2, and 2.3). See [build.md](build.md#older-windows) for OS support.

- ImageWalker 1.0 uses the Windows SDK, the C++ standard library and libfiles.
  Keep ATL, WTL and libdesktop out of its headers, resources and link graph.
  Use the native menu, message loop and controls, not a second GUI framework.
- MSVC compiles the WTL trees as C++20 with `/permissive`.
- ImageWalker 1.0 uses the UTF-8 process code page where available;
  ImageWalker 2.0, 2.2 and 2.3 are Unicode. Convert 1.0's paths explicitly at
  shell, file and clipboard boundaries, and keep the modern
  `include/iw/imaging` API free of `TCHAR`, ATL and WTL types and application
  precompiled headers; historical adapters stay under `include/iw/legacy`.
- For the WTL targets, STL headers precede ATL, WTL and application headers
  in each precompiled header, and `<wininet.h>` precedes `<shlobj.h>`.
- CMake source lists preserve extension case and match one original translation
  unit per implementation.
- Do not introduce application COM servers, ActiveX controls, shell extensions,
  licensed TWAIN code, x86 assembly or obsolete imaging libraries.
- Use operating-system COM clients for shell folders, data objects, drop
  targets and shell links wherever Windows supplies the service.
- In the 2.x apps use the shared ATL-based clipboard format enumerator, which
  keeps independent target-device storage and the standard Skip and Clone
  behaviour. ImageWalker 1.0 puts Unicode file paths in `CF_HDROP`.
- Resource scripts embed the manifest and keep the application icon as the
  lowest-numbered icon group.
- WTL command bars use classic menu rendering. Callbacks run with a valid
  ATL/WTL message context, and alternate message maps use plain handlers with
  an explicit `BOOL& bHandled`, never the `_EX` macros.
- Pointer-sized Win32 values stay pointer-sized through callbacks, messages and
  window data.
- `IW::Min`, `IW::Max` and `IW::Clamp` take integers only; floating-point
  geometry uses type-correct standard operations.

Fetch and build third-party dependencies through `third-party/`; see
[build.md](build.md#dependencies). Link application code with the static CRT.
Keep settings and logs beside the executable, extract embedded help to the
user's temporary folder, and write image output only to locations the user
chooses.

## Formatting

`.editorconfig` records the tab-indented C++ and space-indented project file
conventions. `.clang-format` is the house C++ style - tabs, Allman braces and
120 columns - and preserves include order because the precompiled-header
ordering above is significant. ImageWalker 3.0 and the modern shared code are
formatted with it. `.clang-format-ignore` lists what is never formatted:
vendored code, generated output and the preserved releases - `src10` to
`src23`, the WTL-era headers they share under `include` and their legacy
tests - which keep their historical layout. Tables laid out by hand sit between
`// clang-format off` and `// clang-format on`.

`.\dd.ps1 fmt` formats `include`, `src` and `tests` only, and needs
`clang-format` on `PATH`; Visual Studio installs one in
`VC\Tools\Llvm\x64\bin`. Format `src30` by running that `clang-format -i` on
its files.
