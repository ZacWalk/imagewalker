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

For the full collection, add ATL in the Visual Studio Installer and run
`winget install --id Python.Python.3.13 --exact`, then open a new terminal.
CMake checks for ATL and Python itself in a direct build.

### SDK-only ImageWalker 1.0

The standalone 1.0 build needs MSVC, the Windows SDK, and libfiles, not ATL,
WTL, libdesktop, or Hunspell. From a Visual Studio Developer PowerShell in
the repository root:

```powershell
cmake --preset x64-release -B build\iw10-sdk -DIW_BUILD_10_ONLY=ON "-DIW_OUTPUT_DIR=$PWD\out\iw10-sdk\x64"
cmake --build build\iw10-sdk --target imagewalker10
ctest --test-dir build\iw10-sdk -R '^imagewalker10(-sdk-boundary)?$' --output-on-failure --no-tests=error
```

In the x64-to-x86 developer environment, use `x86-release` and distinct
build/output directories. Both configurations embed Help.

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

The Ninja presets `x64-debug`, `x64-release` and `x86-release` have matching
CTest presets. Configure and build `x86-release` in the
**x64_x86 Cross Tools Command Prompt for VS 2026**; the x86 binaries go to
`exe\x86\` and do not replace the default x64 binaries. The `vs` preset makes
a Visual Studio solution in `build/vs` whose `vs-debug` and
`vs-release` build and CTest presets select Debug and RelWithDebInfo. `ide`
generates that solution, and `--yes` opens it.

The applications are portable: keep the chosen executables, the dictionaries in
`exe\dic` and the optional `imagewalker30-tools.json` together in a writable
folder. Each executable carries its help, and nothing needs installing,
registering or elevation.

### Timed screenshots

Each edition can open a folder or photo, wait ten seconds, save its own
window as a PNG, and exit:

```powershell
& .\exe\imagewalker23.exe --screenshot 'C:\Screenshots\imagewalker-2.3.png' 'C:\Sample Photos'
```

The folder or photo argument is optional. Give the screenshot a `.png` name
in an existing directory. The application does not overwrite existing files
and exits with a nonzero status if it cannot save. Keep the window unminimized
and use a writable copy of the executable for separate screenshot preferences.

### ImageWalker 3.0 MCP fixture sessions

Start `imagewalker30.exe` as an MCP **stdio** server, with stdin and stdout
redirected by the client. The process exits on stdin EOF. There is no network
listener. Use an absolute folder on a local fixed drive that you created for
testing, with an empty `.imagewalker-mcp-fixture` marker and generated PNG,
JPEG or BMP photos. Optional subfolders make browse tests possible. Launch
rejects unmarked roots, aliases (including reparse points and hard links),
and fixtures with more than 256 entries, more than eight nested levels or
photos over 32 MiB. Keep the fixture unchanged while the session is running.
Only entries admitted at startup are browsable.

For example, set this server entry in an MCP client:

```json
{
  "type": "stdio",
  "command": "C:\\ImageWalker\\imagewalker30.exe",
  "args": ["--mcp", "--fixture-root", "C:\\Test Data\\IW30 fixture", "--mcp-allow-capture"]
}
```

| Tool | Purpose |
|---|---|
| `ui_state` | Current relative folder and view, visible items, scan and thumbnail counts, selection, selected-photo decode state and task review state |
| `browse` | Open an admitted folder using `folder`, an exact relative key (`.` for the root, `/` between components) |
| `select_photos` | Select one to sixteen distinct names in the current folder using `names`; preview the last |
| `open_preview` | Open `view`: `convert`, `rename`, `edit` or `sync`; Sync opens with no remote folder or analysis |
| `close_view` | Return to Browse from a task or fullscreen; poll if task analysis has not finished |
| `set_fullscreen` | Set `enabled` after a selected photo loads |
| `screenshot` | Return an inline PNG of the app window, with optional `max_edge` from 128 to 896 pixels; only advertised with `--mcp-allow-capture` |

Poll `ui_state` for a ready scan, thumbnail progress and a ready photo before
capturing. Thumbnails are scheduled for the viewport and nearby items, not the
entire folder at once. The agent uses a cloaked software-rendered window and
never substitutes desktop pixels for a failed capture. It needs a Windows
desktop session with DWM; renderer, theme, DPI and monitor geometry can differ
between hosts. This is not arbitrary UI automation: there are no tools for
input injection, dialogs, file writes, clipboard or external launches. The
interactive INI, saved folders, tools configuration and Undo are not used.
The pinned runtime may create its ordinary log and default empty INI under the
separate `imagewalker30-mcp` identity.

Run the end-to-end protocol smoke in PowerShell 7.4 or newer:

```powershell
.\tools\test-iw30-mcp.ps1 -Exe .\exe\imagewalker30.exe
```

The script creates disposable media and an isolated executable copy under
`build\`. It checks denied paths and capture permissions, navigation,
selection, previews, fullscreen, fixture pixels in the returned PNG and
shutdown on EOF. Use `-KeepCaptures` to retain the generated fixture and
PNGs for inspection.

## Collection release

Install NSIS 3.08 or later and the Windows SDK's SignTool on an x64 build PC.
Select the thumbprint of the current code-signing certificate in the current
user's Personal store. Run from PowerShell 7.4 or later:

```powershell
.\tools\package-release.ps1 -CollectionVersion 3.0.0.1 -CertificateThumbprint <publisher-thumbprint>
```

The script builds and tests both architectures in separate shipping trees,
using the published dependency pins instead of any sibling checkout. It
copies only the five release binaries per architecture, dictionaries, tools
configuration and notices into the payload. It verifies executable version
and architecture and requires valid timestamped Authenticode signatures.
The portable ZIP contains `x86\` and `x64\`; setup installs only the native
set into the current user's writable folder and adds five Start shortcuts.
Setup and its uninstaller are signed. Uninstall leaves user settings, tools
configuration, logs and recovery data intact.
Update and uninstall check managed files before changing the collection and
report the path of any locked or inaccessible file. Close every edition
before upgrading. The preflight does not provide rollback for a later I/O
failure.

The output directory contains the installer, portable ZIP and a hash manifest.
Before offering the binaries publicly, push and tag their exact source revision
in the [ImageWalker repository](https://github.com/ZacWalk/imagewalker) with
the Collection version (`v` followed by the four-part version). The matching
tag must include these build instructions and the dependency pins needed to
obtain the corresponding source and relink (see the
[licence requirements](../README.md#licence)). To relink with a library you have
modified, select its checkout with `FETCHCONTENT_SOURCE_DIR_LIBFILES` or
`FETCHCONTENT_SOURCE_DIR_LIBDESKTOP`.

Before publishing, clean-install and update on Windows 10 and 11. Launch
all five Start shortcuts and verify their About versions, F1 topics and
settings persistence; uninstall and confirm the shortcuts disappear and
settings remain. Extract the portable ZIP to a writable path with spaces
and run from both directories on x64; use a native 32-bit Windows 10
machine or VM for the x86 setup branch. Exercise browse, zoom, and fullscreen in
1.0, descriptions and print preview in 2.x, tagging and metadata in 2.3,
and video/audio, Edit/Convert, Rename/Undo and Sync on expendable copies
in 3.0. Check Windows N separately if advertising its media support.

### Older Windows

For the Collection, advertise Windows 10 and 11 on x86/x64, as supported
by the architecture of the OS. The 1.0 and 2.x targets use Windows 7-era SDK
declarations and a 6.01 subsystem header, but that is not proof of OS
compatibility. The [v145 toolset](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc?view=msvc-180)
does not target Windows 7, 8 or 8.1. In particular its x86 legacy builds
import `GetSystemTimePreciseAsFileTime`, which Windows 7 does not provide.
ImageWalker 3.0 is a modern Windows build. A Windows 7 edition would need
a separate v143 toolchain investigation and tests on Windows
7 SP1 (both architectures, including signed setup, browsing and Help);
do not infer support from the manifest or linker switches.

## Compilation

The three 2.x WTL applications compile as C++20 with `/permissive`, `/W4`
and `/EHsc` through the `iw_flags` interface target. Only these targets
disable C++ module scanning: they use headers, and MSVC's standard-library
modules do not compile with `/permissive`. ImageWalker 1.0 uses
`/permissive-`, `/W4` and `/EHsc` independently. ImageWalker 3.0 sets the same
options and `/guard:cf` outside Debug. Every target, dependencies
included, links the static CRT. Release is `RelWithDebInfo` and keeps PDB files
for crash reports. See [warnings.md](warnings.md) and [porting.md](porting.md).

The 2.x and 3.0 applications consume the header-only `iw_imaging` or
`iw_legacy` interface targets; 1.0 links only SDK libraries and libfiles.
See
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
cancellation, playback without Media Foundation and that its DLLs are not
mandatory imports. It decodes a generated AVI and WAV with no visible window.
It checks pause, seek, stop, and playback in Browse without leaving the item
list. The WAV plays through the default audio endpoint; if there is none,
the suite checks that the player reports the error and skips actual output.

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
| [libdesktop](https://github.com/ZacWalk/libdesktop) | `GIT_TAG` in `CMakeLists.txt` | 2.x and 3.0 |
| [libfiles](https://github.com/ZacWalk/libfiles) | `IW_LIBFILES_REVISION` in `third-party/CMakeLists.txt` | All five |
| Hunspell | `IW_HUNSPELL_VERSION` and its SHA-256 in `third-party/CMakeLists.txt` | 2.0, 2.2 and 2.3 |
| [FFmpeg](https://ffmpeg.org/) | Commit archive and SHA-256 in `third-party/FFmpeg.cmake` | 3.0 playback and Media Edit |
| WTL 10.01 | Vendored in `include/wtl` | 2.0, 2.2 and 2.3 |

A sibling `..\libfiles` is used instead of the pin unless
`IW_USE_SIBLING_DEPS` is disabled. A sibling `..\libdesktop` is used only if
it exposes `ui::style::set_palette`; the fetched pin gets the palette patch
from `third-party/patches` without modifying the sibling checkout.
CI and the shipping build use the pins.
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

ImageWalker 3.0 builds selected FFmpeg decoders and the libraries for local
playback and packet copying. The recipe uses Git for Windows' Bash and a checksum-pinned GNU Make
archive at build time; neither is needed at runtime. Start Windows builds in
the matching Visual Studio developer environment. FFmpeg has no enabled
network or file protocols; the app supplies local input and staged output.
The other editions do not link FFmpeg.

FFmpeg is statically linked under LGPL 2.1 or later. Before distributing a
3.0 binary, include FFmpeg attribution, its license, corresponding source,
and materials and instructions to relink the exact ImageWalker build with a
modified library. Keep the FFmpeg revision and configuration with the
release materials; see [FFmpeg's legal guidance](https://ffmpeg.org/legal.html).

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
