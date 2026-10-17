# Repository layout

| Path | Contents |
|---|---|
| `src10` | ArtMate 1.0 application and tests |
| `src20` | ImageWalker 2.0 application and tests |
| `src22` | ImageWalker 2.2 application and tests |
| `src23` | ImageWalker 2.3 application and tests |
| `src30` | ImageWalker 3.0 application and tests |
| `include/iw` | Header-only code shared by the four WTL applications |
| `include/wtl` | Vendored WTL headers |
| `include/core` | Header-only `util_layout.h`, `util_geometry.h`, `util.h` and `help_file.h`, shared by all five applications |
| `src30/util_color.h` | ImageWalker 3.0 colour primitives |
| `include/iw/ColorButton.h` | Legacy colour control shared by the 2.x trees |
| `third-party` | CMake recipes, versions and patches for the fetched dependencies |
| `Help` | Compiled-help sources for all five applications, with separate 3.0 workflow topics |
| `res-extra` | Historical icons, bitmaps and cursors kept outside the built resource scripts |
| `tools` | Help compiler and checkers, and repository utilities |
| `cmake` | Shared CMake modules |
| `Setup` | Historical installer artwork and licence text |
| `exe` | Executables, PDBs, ini files, logs, dictionaries, tool configuration and CHM output |
| `build` | Generated build trees and UI/image test harnesses |
| `docs` | Repository reference documents |
| `.dd` | Unmodified, pinned shared dd runtime and templates |
| `.dd/state` | Ignored, machine-local CMake preset verification records |
| `dd.psd1` | Target metadata and project command declarations |

Track only `exe/dic` and `exe/imagewalker30-tools.json` under `exe`; keep
generated output, settings and logs out of version control.

## Ownership

Keep reusable code header-only in `include/`; keep anything requiring a
translation unit in its owning application. Do not introduce a shared compiled
ImageWalker library.

Keep each application's resource script, manifest, icon, version identity,
entry point and tests in its directory. Embed `ImageWalker.chm` in every
resource script. Preserve independent builds of the 2.x applications; see
[architecture-2x.md](architecture-2x.md).

For ImageWalker 3.0, use the utility and help headers from `include/core/` and
keep colour primitives in `src30/util_color.h`. Do not include `include/iw`
or link a WTL application. Keep codec dependencies application-owned and place
3.0's help topics in the common `ImageWalker.chm`.
