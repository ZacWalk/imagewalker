# Repository layout

| Path | Contents |
|---|---|
| `src10` | ImageWalker 1.0 application and tests |
| `src20` | ImageWalker 2.0 application and tests |
| `src22` | ImageWalker 2.2 application and tests |
| `src23` | ImageWalker 2.3 application and tests |
| `src30` | ImageWalker 3.0 application and tests |
| `include/iw/imaging` | Modern, platform-independent inline imaging contracts and code |
| `include/iw/legacy` | Header-only 2.x compatibility models and adapters |
| `include/iw` | Other header-only code shared by the WTL applications, such as the shell tree, crash reporter and test runner |
| `include/core` | `help_file.h`, used by all five, and the WTL applications' utility headers |
| `include/wtl` | Vendored WTL headers |
| `tests/imaging` | Shared imaging regression consumer and the ownership and dependency check |
| `cmake` | The shared imaging CMake module |
| `third-party` | CMake recipes, versions and patches for the fetched dependencies |
| `Help` | Compiled-help sources for all five applications, with separate 3.0 topics |
| `res-extra` | Icons, bitmaps and cursors kept outside the built resource scripts |
| `Setup` | Installer artwork and licence text, not used by the build |
| `tools` | Help compiler, checkers and capture script, the project commands and the dd contract test |
| `exe` | Executables, PDBs, ini files, logs, dictionaries, tool configuration and CHM output |
| `build` | Generated build trees |
| `docs` | Repository reference documents |
| `.dd` | The vendored dd runtime; `.dd/state` holds ignored, machine-local preset records |
| `dd.psd1` | Target metadata and project command declarations |

Git tracks only `exe/dic` and `exe/imagewalker30-tools.json` under `exe`; keep
generated output, settings and logs out of version control.

## Ownership

Keep reusable UI, utility and test helpers header-only in `include/`, imaging
contracts and their inline definitions in `include/iw/imaging` and
`include/iw/legacy`, and translation units with the application that owns them.
Compiled codec and platform mechanisms belong in libfiles and libdesktop.
[imaging.md](imaging.md) defines the imaging API, codec selection and adapter
boundaries.

Keep each application's resource script, manifest, icon, version identity,
entry point and tests in its directory, and preserve the 2.x applications'
independent builds; see [architecture-2x.md](architecture-2x.md).

ImageWalker 3.0 uses libdesktop's `core`, `platform`, `render` and `ui`
libraries and `include/iw/imaging`, never the historical compatibility headers,
the shared WTL-era headers in `include/iw` or the geometry helpers in
`include/core`. Its frame, drawn menu bar and text fields come from
`libdesktop::ui`. It draws its own folder tree and plays media into a
libdesktop texture and audio output, so it embeds no native controls. Its
explicit Windows display adapter calls libdesktop's platform image API; all
other image codec work goes through libfiles. Its help topics are part of the
common `ImageWalker.chm`.
