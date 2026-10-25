# Repository layout

| Path | Contents |
|---|---|
| `src10` | ArtMate 1.0 application and tests |
| `src20` | ImageWalker 2.0 application and tests |
| `src22` | ImageWalker 2.2 application and tests |
| `src23` | ImageWalker 2.3 application and tests |
| `src30` | ImageWalker 3.0 application and tests |
| `libiw/include/libiw` | Application-independent public imaging API |
| `libiw/src` | libfiles/SDK adapters, native encoders, metadata and image processing |
| `libiw/compat/iw/imaging` | ATL-based interfaces for the 2.x adapters and the native-page backend |
| `libiw/tests` | Standalone public/native engine tests and the dependency-boundary check |
| `include/iw` | Header-only code shared by the four WTL applications |
| `include/wtl` | Vendored WTL headers |
| `include/core` | Header-only `util_layout.h`, `util_geometry.h`, `util.h` and `help_file.h`, shared by all five applications |
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

Keep reusable UI, utility and test helpers header-only in `include/`. Put shared
imaging implementations and their compatibility declarations in `libiw`; link
its single static target into every application. Keep other translation units
with their owning application. Follow [libiw-plan.md](libiw-plan.md) for the
public API, codec selection and adapter boundaries.

Keep each application's resource script, manifest, icon, version identity,
entry point and tests in its directory. Embed `ImageWalker.chm` in every
resource script. Preserve independent builds of the 2.x applications; see
[architecture-2x.md](architecture-2x.md).

For ImageWalker 3.0, use `include/core/` and the public `libiw` headers, not the
ATL compatibility headers or a WTL application. Keep codec acquisition in
libfiles; `third-party/` pins that library and the application-owned Hunspell
recipe. Place 3.0's help topics in the common `ImageWalker.chm`.
