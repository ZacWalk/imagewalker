# Port configuration

The historical applications are current x64 Windows builds that retain their
original WTL structure and presentation. These are the constraints that keep
that true.

- MSVC compiles the WTL trees as C++20 with `/permissive`.
- ArtMate 1.0 is MBCS. ImageWalker 2.0, 2.2 and 2.3 are Unicode.
- STL headers precede ATL, WTL and application headers in each precompiled
  header. `<wininet.h>` precedes `<shlobj.h>`.
- CMake source lists preserve extension case and match one original translation
  unit per implementation.
- App-authored COM servers, ActiveX controls, shell extensions, licensed TWAIN
  code, x86 assembly and obsolete imaging libraries are absent. The subsystems
  that went with them are listed in [history.md](history.md).
- Operating-system COM clients — shell folders, data objects, drop targets,
  shell links — remain wherever Windows supplies the service.
- Resource scripts embed the manifest and keep the application icon as the
  lowest-numbered icon group.
- WTL command bars use classic menu rendering, and alternate message maps use
  plain handlers with an explicit `BOOL& bHandled`.
- Pointer-sized Win32 values stay pointer-sized through callbacks, messages and
  window data.
- Shared numeric helpers are used only with their supported integer types;
  floating-point geometry uses type-correct standard operations.

Third-party dependencies are fetched and built by `third-party/`; their versions
are listed in [build.md](build.md). Application code links the static CRT and
writes no runtime files outside its executable directory, except files the user
explicitly chooses.

## Formatting

`.editorconfig` records the existing tab-indented C++ and space-indented project
file conventions. `.clang-format` provides an Allman-brace, four-column C++
profile for changed code; it deliberately preserves include order because the
precompiled-header ordering above is significant. `.clang-format-ignore`
excludes vendored code and generated output. Do not reformat entire historical
trees as part of unrelated edits; the existing source has not been normalised
and CI does not impose a whole-tree formatting gate.
