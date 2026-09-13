# Repository layout

| Path | Contents |
|---|---|
| `src10` | ArtMate 1.0 application and tests |
| `src20` | ImageWalker 2.0 application and presentation profile |
| `src22` | ImageWalker 2.2 application and presentation profile |
| `src23` | ImageWalker 2.3 application and presentation profile |
| `src30` | Independent ImageWalker 3.0 application and tests |
| `include/iw` | Header-only code shared by the four WTL applications |
| `include/wtl` | Vendored WTL headers |
| `include/core` | Header-only `util_layout.h`, `util_geometry.h` and `util.h`, shared by all five applications |
| `src30/util_color.h` | Colour primitives owned by the modern application |
| `include/iw/ColorButton.h` | Legacy colour control shared by the 2.x trees |
| `third-party` | CMake recipes, versions and patches for the fetched dependencies |
| `Help` | Compiled-help sources for all five applications, with separate 3.0 workflow topics |
| `res-extra` | Historical icons, bitmaps and cursors kept outside the built resource scripts |
| `tools` | Help compiler and checkers, and repository utilities |
| `cmake` | Shared CMake modules |
| `Setup` | Historical installer artwork and licence text; no active installer recipe |
| `exe` | Executables, PDBs, ini files, logs, dictionaries, tool configuration and CHM output |
| `build` | Generated build trees and UI/image test harnesses |
| `docs` | Repository reference documents |
| `.dd` | Unmodified, pinned shared dd runtime and templates |
| `.dd/state` | Ignored, machine-local CMake preset verification records |
| `dd.psd1` | Target metadata and project command declarations |

Of `exe`, only `exe/dic` and `exe/imagewalker30-tools.json` are tracked; the
rest is build output and is ignored.

## Ownership

There is no shared compiled ImageWalker library. Reusable code is header-only in
`include/`; anything that needs a translation unit stays in the application that
owns it.

Each application directory owns its resource script, manifest, icon, version
identity, entry point and tests. The three 2.x directories intentionally carry
parallel source files so that each application remains independently buildable —
see [architecture-2x.md](architecture-2x.md).

ImageWalker 3.0 takes `util_layout.h`, `util_geometry.h` and `util.h` from
`include/core/` and owns its colour header. It uses none of `include/iw`, links
only Windows platform libraries plus application-owned codecs. Its own help
topics are included in the shared `ImageWalker.chm`.
