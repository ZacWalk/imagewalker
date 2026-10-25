# Modern Windows build constraints

Follow these rules when building the historical applications on modern
64-bit Windows.

- MSVC compiles the WTL trees as C++20 with `/permissive`.
- ArtMate 1.0 is MBCS. ImageWalker 2.0, 2.2 and 2.3 are Unicode.
- Convert ArtMate paths explicitly at its imaging adapter. Keep the public
  `libiw` API free of `TCHAR`, ATL/WTL types and application precompiled headers.
- STL headers precede ATL, WTL and application headers in each precompiled
  header. `<wininet.h>` precedes `<shlobj.h>`.
- CMake source lists preserve extension case and match one original translation
  unit per implementation.
- Do not introduce application COM servers, ActiveX controls, shell extensions,
  licensed TWAIN code, x86 assembly or obsolete imaging libraries.
- Use operating-system COM clients for shell folders, data objects, drop
  targets and shell links wherever Windows supplies the service.
- Resource scripts embed the manifest and keep the application icon as the
  lowest-numbered icon group.
- WTL command bars use classic menu rendering, and alternate message maps use
  plain handlers with an explicit `BOOL& bHandled`.
- Pointer-sized Win32 values stay pointer-sized through callbacks, messages and
  window data.
- Shared numeric helpers are used only with their supported integer types;
  floating-point geometry uses type-correct standard operations.

Fetch and build third-party dependencies through `third-party/`; see
[build.md](build.md) for the dependency versions. Link application code with
the static CRT. Keep settings and logs beside the executable, extract embedded
help to the user's temporary folder, and write image output only to locations
the user chooses.

## Formatting

`.editorconfig` records the existing tab-indented C++ and space-indented project
file conventions. `.clang-format` provides an Allman-brace, four-column C++
profile for changed code; it deliberately preserves include order because the
precompiled-header ordering above is significant. `.clang-format-ignore`
excludes vendored code and generated output. Do not reformat entire historical
trees as part of unrelated edits or impose a whole-tree formatting gate.
