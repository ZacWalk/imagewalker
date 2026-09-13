# ImageWalker

[![Build](https://github.com/ZacWalk/imagewalker/actions/workflows/ci.yml/badge.svg)](https://github.com/ZacWalk/imagewalker/actions/workflows/ci.yml)

ImageWalker was a Windows image browser and viewer written by
[Zac Walker](https://rethinkify.com/) between 1998 and 2006. This repository
preserves its released versions as standalone 64-bit applications that build and
run on current Windows, and adds a modern successor.

| Directory | Application | Year | Character |
|---|---|---:|---|
| `src10` | ArtMate 1.0 | 1998 | Read-only thumbnail browser and viewer |
| `src20` | ImageWalker 2.0 | 2001 | 2.x feature set in the 2001 presentation |
| `src22` | ImageWalker 2.2 | 2003 | 2.x feature set in the 2003 presentation |
| `src23` | ImageWalker 2.3 | 2006 | 2.x feature set in the 2006 dark presentation |
| `src30` | ImageWalker 3.0 | 2026 | Separate modern implementation |

The three 2.x applications share one browsing, search, edit, print and
batch-tool feature set. Their chrome, panes, colours, logos and mode selectors
preserve the appearance of each release.

Every application is portable and free. Settings sit beside the executable, and
there is no registration, reminder screen or feature limit.

## Build

PowerShell 7.4+, Git, Visual Studio 2026 with C++ and ATL, CMake 3.28+, Ninja
and Python 3.9+. The pinned shared [dd driver](https://github.com/ZacWalk/dd)
drives CMake; ImageWalker's own CMake recipes fetch and build the third-party
dependencies.

```powershell
.\dd.ps1 build [debug|release|both]
.\dd.ps1 launch iw30
.\dd.ps1 test
```

Application IDs are `iw10`, `iw20`, `iw22`, `iw23` and `iw30`. `launch` opens a
persistent session; shared `run` is bounded to 120 seconds. Executables and the
shared help file (with separate 3.0 topics) are written to `exe\`.

## Documents

| Document | Subject |
|---|---|
| [history.md](docs/history.md) | The releases, and what the port left behind |
| [versions.md](docs/versions.md) | Release identity and presentation |
| [features.md](docs/features.md) | What the five applications do |
| [workflows-3.0.md](docs/workflows-3.0.md) | ImageWalker 3.0 file workflows |
| [build.md](docs/build.md) | Toolchain, build, tests, help and dependencies |
| [layout.md](docs/layout.md) | Repository ownership and file locations |
| [architecture-2x.md](docs/architecture-2x.md) | How the three 2.x trees relate |
| [porting.md](docs/porting.md) | Compiler, resource and Win32 constraints |
| [app-rules.md](docs/app-rules.md) | Invariants shared by the applications |
| [reliability.md](docs/reliability.md) | Reliability properties of the file, image and threading paths |
| [warnings.md](docs/warnings.md) | Warning policy and triage |
| [todo.md](docs/todo.md) | Outstanding development |

MIT licensed. ImageWalker's successor is [Diffractor](https://diffractor.com/).
