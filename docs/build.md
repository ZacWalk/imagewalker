# Build and validation

The [README](../README.md#build) lists the prerequisites and the everyday
commands. CMake configures the five application targets and builds their
dependencies from source; the Python help tools use only the standard library.

## Toolchain

`dd.psd1` declares the minimum Visual Studio, CMake and Python versions the
README lists, and Visual Studio's ATL component,
`Microsoft.VisualStudio.Component.VC.ATL`; `doctor`, builds and `toolchain`
check them. Inspect the plan before installing anything, and run the
installation yourself in an elevated PowerShell:

```powershell
.\dd.ps1 doctor --json
.\dd.ps1 toolchain --dry-run
.\dd.ps1 toolchain --yes
```

To install by hand, add ATL in the Visual Studio Installer and run
`winget install --id Python.Python.3.13 --exact`, then open a new terminal.
CMake checks for ATL and Python itself in a direct build.

## Commands

```powershell
.\dd.ps1 build debug --app iw30
.\dd.ps1 launch iw23 -- 'C:\Photos with spaces'
.\dd.ps1 run iw30 -- /test
.\dd.ps1 test --app iw30
.\dd.ps1 test --label imagewalker-imaging
.\dd.ps1 check-help --yes
.\dd.ps1 ide --yes
```

`launch` and `run` take `iw30` when no target is named. Builds write
`exe\imagewalker<ver>.exe`, and `exe\imagewalker<ver>d.exe` for Debug.

`launch` builds the target's Release executable and starts it as an
independent session from the project root, returning its PID, start time and
stdout and stderr log paths; it waits for nothing and sets no timeout.
Arguments after `--` are passed on; quote paths that contain spaces. `run`
waits for the process and kills its tree after 120 seconds, or after
`--timeout` seconds. Use `launch`, or F5 in VS Code, for interactive work.

The project commands `check-help` and `test-app` accept `--dry-run` and
`--json`, and need `--yes` to run; `test-app --app ID` runs `test --app ID`.

The presets are `x64-debug` and `x64-release`, both Ninja with matching CTest
presets, and `vs`, a Visual Studio solution in `build/vs` whose `vs-debug` and
`vs-release` build and CTest presets select Debug and RelWithDebInfo. `ide`
generates that solution, and `--yes` opens it.

The applications are portable: keep the chosen executables, the dictionaries in
`exe\dic` and the optional `imagewalker30-tools.json` together in a writable
folder. Each executable carries its help, and nothing needs installing,
registering or elevation.

## Compilation

The four WTL applications compile as C++20 with `/permissive`, `/W4` and
`/EHsc` through the `iw_flags` interface target. ImageWalker 3.0 sets its own
flags in `src30/CMakeLists.txt`: `/permissive-`, `/W4`, `/EHsc`, `/guard:cf`
outside Debug, and no warning suppressions. Every target, dependencies
included, links the static CRT. Release is `RelWithDebInfo` and keeps PDB files
for crash reports. See [warnings.md](warnings.md) and [porting.md](porting.md).

The applications consume the header-only `iw_imaging` or `iw_legacy` interface
targets and link libfiles and libdesktop directly; see
[imaging.md](imaging.md). libfiles owns codec callbacks and their exception
policy. Runtime-dispatched AVX2 code is compiled privately by libdesktop;
application headers and targets must not acquire `/arch:AVX2`.

Every application target states its exception unwinding explicitly. Dependency
recipes may replace CMake's cached default flags; they must not remove the
destructor unwinding that worker and file-operation recovery depend on.

## Tests

[CI](../.github/workflows/ci.yml) runs `doctor`, `dd.ps1 test` and
`check-help --yes` on every push and pull request, on GitHub's Windows Server
2025 image with Visual Studio 2026, and keeps the logs and JUnit reports. dd
stops a build after ten minutes, which building all five releases from nothing
exceeds on that runner, so CI first builds both trees with dd's presets and
compiler environment, and `test` finds them current.

Each executable accepts `/test` and returns its failure count. `dd.ps1 test`
builds Debug and Release, runs CTest in both, then starts each GUI target in
each configuration and checks that it closes cleanly without a crash report.
CTest entries have explicit deadlines of 30 or 120 seconds:

| Test | Other labels (besides `headless`) | Checks |
|---|---|---|
| `imagewalker10` ... `imagewalker30` | `iw10` ... `iw30` | Each application's `/test` suite, run serially from `exe\` |
| `imagewalker-imaging` | `imagewalker-imaging` and every application's | The shared imaging suite |
| `imagewalker-headers` | `imagewalker-imaging` and every application's | Several translation units that include only the shared headers, in different orders, and share process-wide diagnostic state |
| `imagewalker-boundary` | `imagewalker-imaging` and every application's | Source ownership, direct dependencies and public-header isolation; see [imaging.md](imaging.md#ownership) |
| `dd-contract` | none | The dd manifest and project-command contract |
| `help-compiler` | none | The help compiler's regression tests |
| `imagewalker30-icon` | `iw`, `iw30` | Pinwheel overlap, transparent padding and unchanged historical icons |
| `imagewalker-test-windows` | Every preserved application's | Real popup/modal offscreen policy and hidden fullscreen geometry |

Every entry also has the common `headless` label.
`.\dd.ps1 test --label headless` builds both configurations and runs all five
applications, shared imaging, native-window, help and driver checks without
the visible application smoke launches. Test presets print passing output
too: the suites' own case/check totals, not just CTest's executable count.
After building, use `ctest --preset x64-debug -L headless -V --no-tests=error`
or `x64-release` to rerun without rebuilding.

The preserved releases' `/test` harness installs a policy on its own test
thread only: real native dialogs, navigation popups and nested modal loops
stay outside the desktop, have no taskbar/Alt+Tab entries and cannot activate.
Creation or centring cannot briefly show them first. The policy's regression
exercises a real popup and modal dialog. Native fullscreen tests instead keep
their windows unshown at the actual monitor coordinates: all screen, client,
content, maximize and restoration assertions remain, with hidden-state checks
throughout. None of those workflow cases is filtered out.
ImageWalker 3.0's frame tests use its hidden platform/test frames.
Direct `/test` calls to these GUI executables must redirect both streams and
be awaited, for example
`& .\exe\imagewalker23d.exe /test 2>&1 | Out-Host; $LASTEXITCODE`.

`test --app ID` builds both configurations, runs that application's CTest label
and starts only that application. `--label` and `--name` select by CTest label
and test-name regular expression and start no GUI; an empty selection fails.
`dd-contract` and `help-compiler` have only the `headless` label, so select that
label or their names to include them without GUI smoke checks;
`ctest --test-dir build\x64-debug -R dd-contract` runs one in an existing build
tree. The headless gate covers every CTest entry; only the unfiltered gate
adds visible application smoke checks.

`.\exe\imagewalker30d.exe /test-no-clipboard | Out-String`, or
`imagewalker30.exe` for Release, runs the 3.0 suite without its real clipboard
round trip, where the system clipboard is unavailable. `/test` and CTest keep
it.

The 3.0 media suite checks metadata, poster pixels, malformed inputs,
cancellation, optional-component failures and that no Media Foundation DLL is
a mandatory import. It decodes a generated AVI and WAV with no window, and
plays the AVI through pause, seek, the end of the media and stop. The WAV plays
through the default audio endpoint; without one the suite expects the visible
error and reports that check as skipped.

The native 2.x print tests need the local **Microsoft Print to PDF** printer,
with Microsoft's PDF driver and the `PORTPROMPT:` port. They never fall back to
the default or a physical printer, and report a missing or unsafe printer as a
test-environment failure.

## Help

Python 3.9+ builds `exe\ImageWalker.chm` from `Help\` with
`tools/chmbuild.py`; HTML Help Workshop is not used.

Every resource script embeds the built file as the `IMAGEWALKER_HELP` RCDATA
resource. The top-level `CMakeLists.txt` puts `exe\` on the resource
compiler's include path, defines `IW_EMBED_HELP` on each resource script - per
source, because Visual Studio generators pass the resource compiler the C++
definitions - and recompiles each script when the help changes. At run time
`include/core/help_file.h` writes the resource to the user's temporary folder
as `ImageWalker-<content hash>.chm` and opens that copy, never a `.chm` beside
the executable: HTML Help shows blank pages for one extracted from a downloaded
zip, which carries the mark of the web, and for one on a network share.
Without a built help file the executables carry none, and say so when help is
requested.

```powershell
.\dd.ps1 check-help --yes
python tools\chmdump.py exe\ImageWalker.chm full
```

`check-help` builds the `iw_check_help` target, which runs, with CMake's
Python, the compiler's regression tests, `tools/checkhelp.py` and a strict
build. `checkhelp.py` verifies context IDs, aliases and topic links; the strict
build rejects missing files, broken local links and invalid help configuration,
and leaves an existing CHM untouched when it does. For an interactive F1
check, `.\tools\helptest.ps1 -Exe .\exe\imagewalker10d.exe` checks the help
window that application owns, fails with a nonzero exit code, and closes only
the application it started.

## ImageWalker 3.0 icon

`python tools\generate_icon.py` regenerates `src30\Res\ImageWalker.ico`: four
overlapping squares with cyclic pinwheel occlusion, in nine desktop sizes.
`--check` verifies its bytes without writing; `--output tmp\icon-128.png --size 128`
exports another size. Python 3.9+'s standard library and the vendored
`tools\iconlib.py` are sufficient; no sibling checkout or graphics package is needed.
`python tools\test_icon.py` checks the overlap at small and large sizes and the
hashes of all fifteen historical ICOs. No ArtMate/1.0, 2.0, 2.2 or 2.3 artwork is
regenerated or replaced.

## Dependencies

Dependencies are ImageWalker's own CMake recipes, with no Git submodules:
`dependencies = @{ owner = 'application' }` in `dd.psd1` leaves acquisition to
them, so `dd dep list` and `dd dep install` only report that ownership, and
`dd dep update` or any named dependency change is refused.

| Dependency | Pinned in | Used by |
|---|---|---|
| [libdesktop](https://github.com/ZacWalk/libdesktop) | `GIT_TAG` in `CMakeLists.txt` and `src30/CMakeLists.txt` | All five |
| [libfiles](https://github.com/ZacWalk/libfiles) | `IW_LIBFILES_REVISION` in `third-party/CMakeLists.txt` | All five |
| Hunspell | `IW_HUNSPELL_VERSION` and its SHA-256 in `third-party/CMakeLists.txt` | 2.0, 2.2 and 2.3 |
| WTL 10.01 | Vendored in `include/wtl` | 1.0, 2.0, 2.2 and 2.3 |

A sibling checkout at `..\libdesktop` or `..\libfiles` is built instead of the
pin, whatever its HEAD; CI and a fresh clone build the pins.
`third-party/CMakeLists.txt` owns every recipe, version, patch, archive hash,
option and linking adapter but those of the codecs, which libfiles builds by
its own recipes: libjpeg-turbo, libpng over zlib-ng, libwebp, giflib and
libtiff, with NASM when none is on the path. libfiles pins their versions and
SHA-256 hashes in its
[cmake/dd-dependencies.json](https://github.com/ZacWalk/libfiles/blob/main/cmake/dd-dependencies.json);
ImageWalker builds them as of its pinned commit. `iw::jpeg`, `iw::png`,
`iw::zlib`, `iw::webp`, `iw::gif` and `iw::tiff` name its codec targets. Its
HEIF, JPEG XL and camera raw codecs are not built: ImageWalker decodes those
formats through the installed Windows codecs. WebP needs no Windows codec.
Archives take their extraction time, so changing a pin rebuilds the objects
that depend on it.

## The dd driver

`dd.ps1` and `.dd/` are vendored unmodified from
[ZacWalk/dd](https://github.com/ZacWalk/dd) commit
`4d34f8104128b238fe20b337057fe67a0b6dbd97`, which follows the v0.2.5 release.
Replace the whole runtime through a reviewed change, checked against that commit
and the per-file hashes in [dd-upstream.json](dd-upstream.json); never fork it
for application behaviour. `dd self-update` refuses a project-pinned driver, and
nothing needs installing globally or adding to a profile.

`dd.psd1` declares the five targets and the project commands `check-help` and
`test-app`, implemented in `tools/dd-project.ps1`. Everything else comes from
dd, whose [commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands)
and [MCP server](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md) are
documented in its own repository.

`clean` deletes only the preset build trees it can verify, which it records in
the ignored `.dd/state/`, and refuses foreign caches, tracked files and linked
paths. Run `clean --dry-run` first. Never delete all of `build/`, which can
hold trees dd did not create.
