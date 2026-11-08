# Repository layout

| Path | Contents |
|---|---|
| `src10` | ImageWalker 1.0 application and tests |
| `src20` | ImageWalker 2.0 application and tests |
| `src22` | ImageWalker 2.2 application and tests |
| `src23` | ImageWalker 2.3 application and tests |
| `src30` | ImageWalker 3.0 application and tests |
| `include/iw/imaging` | Modern, platform-independent inline imaging code and contracts |
| `include/iw/legacy` | Header-only 2.x compatibility models and adapters |
| `tests/imaging` | Shared imaging regression consumer and ownership/direct-dependency check |
| `include/iw` | Header-only code shared by the four WTL applications |
| `include/wtl` | Vendored WTL headers |
| `include/core` | Historical WTL utility headers retained by the 1.x/2.x applications; not part of modern imaging or 3.0 geometry |
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

Keep reusable UI, utility and test helpers header-only in `include/`. Put
imaging contracts and their inline definitions in `include/iw/imaging` and
`include/iw/legacy`; compiled codec and platform mechanisms belong in libfiles
and libdesktop. There is no shared application object library. Keep translation units with their owning
application. Follow [libiw-plan.md](libiw-plan.md) for the public API, codec
selection and adapter boundaries.

Keep each application's resource script, manifest, icon, version identity,
entry point and tests in its directory. Embed `ImageWalker.chm` in every
resource script. Preserve independent builds of the 2.x applications; see
[architecture-2x.md](architecture-2x.md).

For ImageWalker 3.0, use libdesktop's `core`, `platform`, `render` and `ui`
contracts and `include/iw/imaging`, not the historical compatibility headers or
WTL utility geometry. The application frame and drawn menu bar come from
libdesktop::ui; the folder tree remains an application-owned Win32 child. Keep
ordinary codec acquisition in libfiles; the explicit Windows display adapter
calls libdesktop's platform image API. `third-party/` pins the
library and the application-owned Hunspell recipe. Place 3.0's help topics in
the common `ImageWalker.chm`.
