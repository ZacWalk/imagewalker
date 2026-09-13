# Build and validation

64-bit Windows, PowerShell 7.4+, Git, Visual Studio 2026 with C++ and ATL,
CMake 3.28+, Ninja and Python 3.9+. CMake configures five executable targets and
builds their dependencies from source. The Python help tools use only the
standard library.

Inspect the declared toolchain before installing anything; run installation
yourself in an elevated PowerShell when it is needed.

```powershell
.\dd.ps1 toolchain --dry-run
.\dd.ps1 toolchain --yes
.\dd.ps1 doctor --json
```

`dd.psd1` declares Visual Studio 18.0+ with
`Microsoft.VisualStudio.Component.VC.ATL`, CMake 3.28+ and Python 3.9+. `doctor`,
builds and toolchain planning check these. For a manual install, add ATL in the
Visual Studio Installer and run
`winget install --id Python.Python.3.13 --exact`, then open a new terminal.
CMake keeps its own ATL and Python checks for direct builds.

## Commands

```powershell
.\dd.ps1 build [debug|release|both]
.\dd.ps1 build debug --app iw30
.\dd.ps1 launch iw30
.\dd.ps1 launch iw23 -- 'C:\Photos with spaces'
.\dd.ps1 test
.\dd.ps1 test --app iw30
.\dd.ps1 check-help --yes
.\dd.ps1 ide --yes
.\dd.ps1 env
```

Application IDs are `iw10`, `iw20`, `iw22`, `iw23` and `iw30`; `iw30` is the
default. Products are `exe\imagewalker<ver>.exe` and
`exe\imagewalker<ver>d.exe`.

Distribution is portable: keep the selected executables, `ImageWalker.chm`,
the optional tools configuration and dictionaries together in a writable
folder. The unused NSIS recipe has been retired; there is no supported
Program Files installer, registry registration or elevation requirement.

`launch` builds the selected Release target and starts an independent GUI
session from the project root, returning the PID, start timestamp and
file-backed stdout/stderr log paths. It does not wait for readiness and imposes
no session timeout. Arguments after `--` are forwarded; quote paths containing
spaces. Shared `run`, for example `dd.ps1 run iw30 -- /test`, waits for exit and
kills the process tree after 120 seconds (`--timeout` changes the bound). Use
`launch` or VS Code F5 for interactive work, not bounded `run`.

`check-help` and the compatibility command `test-app` accept `--dry-run` and
`--json`, and require `--yes` to execute. `launch`, `build` and `test` do not.

Presets are `x64-debug` and `x64-release`, both Ninja with matching CTest
presets, plus a `vs` preset generating a Visual Studio solution in `build/vs`.
The `vs-debug` and `vs-release` build and CTest presets select Debug and
RelWithDebInfo respectively.

## Compilation

The four WTL applications compile as C++20 with `/EHsc`, `/permissive`, `/W4` and the
static CRT through the `iw_flags` interface target. ImageWalker 3.0 compiles
independently with `/permissive-`, `/W4`, `/EHsc` and `/guard:cf`, and takes no
warning suppressions. Release is `RelWithDebInfo` and keeps PDB files for crash
reports. See [warnings.md](warnings.md) and [porting.md](porting.md).

Exception unwinding is set explicitly on every application target. Dependency
recipes may replace CMake's cached default flags; they must not remove the
destructor unwinding that worker and file-operation recovery depend on.

## Tests

The GitHub Actions workflow in `.github/workflows/ci.yml` runs the declared
toolchain check, full `dd.ps1 test`, and strict help validation on pushes and
pull requests. It uses the Windows Server 2025 / Visual Studio 2026 x64 image
(including ATL), with read-only repository permissions, and retains test logs
and JUnit reports for 14 days. No self-hosted runner or extra credentials are
required.

Each executable accepts `/test` and returns its failure count. `dd.ps1 test`
builds Debug and Release, runs CTest in both, then smoke-tests all five GUI
targets in each configuration with crash-file checks and bounded cleanup.

CTest runs six entries serially from `exe\` with 120-second deadlines: one per
application, labelled `iw10` through `iw30`, plus `dd-contract`, which runs
`tools/test-dd.ps1` over driver metadata, application-owned dependencies,
command previews, and the rejection of invalid IDs and unconfirmed writes.

`dd.ps1 test --app iw30` builds both configurations and selects that target's
CTest label. `--label` and `--name` filter further, and an empty selection
fails. Filtered runs omit the GUI smoke tests, so they do not replace a full
`dd.ps1 test`.

The 3.0 media suite always checks metadata, poster pixels, malformed inputs,
cancellation, optional-component failures and the absence of mandatory Media
Foundation DLL imports. Set `IW30_TEST_NATIVE_PLAYBACK=1` in the test process
on an interactive Windows desktop to additionally exercise actual EVR
play/pause/seek/stop behavior. A normal headless run reports this rendering
check as skipped rather than claiming that a device was exercised.

The native 2.x print tests require the local **Microsoft Print to PDF**
printer using Microsoft's PDF driver and `PORTPROMPT:`. They never fall back
to the default or a physical printer. Missing or unsafe printer configuration
is reported as a test-environment failure.

## Help

Python 3.9+ builds `exe\ImageWalker.chm` from `Help\` through
`tools/chmbuild.py`. HTML Help Workshop is withdrawn and is not used.

```powershell
.\dd.ps1 check-help --yes
python tools\chmdump.py exe\ImageWalker.chm full
```

`tools/checkhelp.py` verifies context IDs, aliases and topic links; the strict
build rejects missing files, broken local links and invalid help configuration.
The `iw_check_help` CMake target runs both with CMake's discovered Python.

## Dependencies

Dependencies stay application-owned: `dependencies = @{ owner = 'application' }`
in `dd.psd1` leaves acquisition with ImageWalker, and
`third-party/CMakeLists.txt` owns every recipe, version, patch, archive hash,
option and linking adapter. `dd dep list` and `dd dep install` report ownership
and inventory; named dependency mutations are rejected. There are no Git
submodules.

| Dependency | Version | Acquired by | Linked by |
|---|---|---|---|
| zlib | 1.3.1 | FetchContent, git tag | All WTL apps |
| libjpeg-turbo | 3.1.2 | ExternalProject, git tag | All WTL apps |
| libpng | 1.6.50 | FetchContent, git tag | All WTL apps |
| libtiff | 4.7.0 | FetchContent, git tag | All WTL apps |
| giflib | 5.2.2 | Archive URL with SHA-256 | All WTL apps |
| expat | 2.7.1 | FetchContent, git tag | 2.x only |
| libexif | 0.6.26 | FetchContent, git tag | 2.x only |
| hunspell | 1.7.3 | FetchContent, git tag | 2.x only |
| libwebp | 1.6.0 | FetchContent, git tag | 3.0 only |

Unused openjpeg and sqlite recipes have been retired: no application needs
JPEG 2000 or the removed thumbnail cache. ImageWalker 3.0 owns its static WebP
codec through `third-party/webp.cmake`; it does not depend on a separately
installed WIC WebP extension.

## The dd driver

`dd.ps1` and `.dd/` are vendored without modification from the
[ZacWalk/dd v0.1.0 release](https://github.com/ZacWalk/dd/releases/tag/v0.1.0),
commit `e68d6d53a99094f7f034353dfde0a7fdc0fe2ca5`. Update the whole runtime
through a reviewed change, checking it against the release `dd.zip.sha256` and
the per-file hashes in `release.json`; do not fork it for application behaviour.
`dd self-update` is refused for this project-pinned driver by design. No global
installation or profile change is needed.

`dd.psd1` declares the five targets and the project commands `check-help` and
`test-app`, implemented in `tools/dd-project.ps1`. Everything else comes from
the shared driver.

The optional MCP adapter in `.dd/mcp/` is PowerShell-based and needs no Node.js.
`dd mcp` prints its stdio configuration; `dd mcp --register` writes
`.vscode/mcp.json` only when that file is absent.

`clean` resolves binary directories from the declared presets and records
verified CMake metadata in ignored `.dd/state/`. Use `clean --dry-run` first:
missing or stale state, foreign caches, tracked files, linked paths and edited
or unrecognised dependency caches all block it, and the application-owned
dependency caches frequently do. Never remove all of `build/`, which also holds
test harnesses and reference assets.

`ide` uses the `vs` preset and its `build/vs` directory; `--yes` opens the
solution. `env` returns environment data; importing it into a calling shell
needs the optional shared profile function. `init` scaffolds new empty projects.
