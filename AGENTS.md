# ImageWalker agent notes

Build ArtMate 1.0 and ImageWalker 2.0, 2.2, 2.3 and 3.0 as standalone x64
applications from `src10`, `src20`, `src22`, `src23` and `src30`.
Describe each release by its name, year and feature additions, not as a
presentation of another version or a snapshot of the code. Keep development
guidance instructional rather than a record of completed changes.

## Reference documents

Start with [README.md](README.md), which indexes every document. The ones that
constrain a code change most often:

| Document | Read before |
|---|---|
| [docs/app-rules.md](docs/app-rules.md) | Any change to settings, logging, workers or resources |
| [docs/porting.md](docs/porting.md) | Any change to the WTL trees |
| [docs/reliability.md](docs/reliability.md) | Any change to file writes, pixels, metadata or threading |
| [docs/libiw-plan.md](docs/libiw-plan.md) | Any change to shared image loading, codecs or manipulation |
| [docs/workflows-3.0.md](docs/workflows-3.0.md) | Any change to the ImageWalker 3.0 task views |
| [docs/build.md](docs/build.md) | Any change to CMake, presets, tests, help or dependencies |
| [docs/todo.md](docs/todo.md) | Picking up outstanding work |
| [docs/known-issues.md](docs/known-issues.md) | Fixing or finding anything a user of a release would notice |

## Commands

```powershell
.\dd.ps1 build [debug|release|both]
.\dd.ps1 test
.\dd.ps1 test --app iw30
.\dd.ps1 launch iw30
.\dd.ps1 check-help --yes
```

Target IDs are `iw10`, `iw20`, `iw22`, `iw23` and `iw30`. Shared `run` is
time-bounded; use `launch` for persistent GUI sessions. `doctor` and `toolchain`
check the prerequisites declared in `dd.psd1`. Keep the pinned `dd.ps1` and
`.dd/` runtime unmodified; project commands belong in `tools/dd-project.ps1`.
Dependencies stay application-owned CMake recipes, not submodules. `clean`
resolves preset directories but refuses unsafe or unrecognised caches: preview
only, unless deletion is explicitly authorised, and never delete `build/`
wholesale. `.dd/state/` is generated local state.

## Invariants most easily broken

These are the ones a change breaks silently. Everything else lives in the
documents above; do not restate them here, because a copy drifts.

- Keep `include/` header-only. Shared imaging belongs in the static `libiw`
  target; its public API must not depend on an application, ATL or WTL.
- WTL `_EX` message macros are not used in alternate message maps.
- Floating-point calculations do not call the integer-only `IW::Min`, `IW::Max`
  or `IW::Clamp` helpers.
- Temporary output is committed explicitly with `CFileTemp::Close`; destruction
  aborts incomplete output. Rollback never overwrites an existing file.
- Pixel storage tags change only when the corresponding pixels change, and
  metadata that cannot be represented losslessly is kept as opaque bytes.
- Alpha-bearing pages are read with `GetLine`; `RenderLine` is for compositing.
- Worker exception handling stays inside the processing loop, so one malformed
  file cannot retire a worker, and completion paths always arrange the next
  request — including stale-result and cancellation paths.
- Pointer-size and worker-completion ownership rules live in
  [docs/porting.md](docs/porting.md) and [docs/app-rules.md](docs/app-rules.md).
- In Convert, Rename and Sync, a reviewed plan is immutable once analysed, a
  stale generation is dropped rather than merged, Cancel is partial completion
  rather than rollback, and a destination appearing between Review and Run is
  never silently overwritten.
- A configured external tool is launched only from a discovered executable path.
- Resource edits preserve the app icon as the lowest-numbered icon resource, and
  both Debug and Release are compiled by the test command.
- Help opens from the copy `include/core/help_file.h` writes from the embedded
  resource, never from a `.chm` beside the executable, which a downloaded zip
  leaves blank.

## Validation

Validate source edits with the narrowest relevant test, then run `.\dd.ps1 test`.
Validate help edits with `.\dd.ps1 check-help --yes`, which runs the link checker
and the strict help build described in [docs/build.md](docs/build.md).

## dd build system

ImageWalker vendors the [dd](https://github.com/ZacWalk/dd) build driver
unmodified as `dd.ps1` and `.dd/`, with file hashes recorded in
[docs/dd-upstream.json](docs/dd-upstream.json). Project settings live in
`dd.psd1`; dependency pins live in ImageWalker's application-owned CMake recipes
(`dependencies.owner = 'application'`). `.\dd.ps1 commands --json` reports the
ImageWalker-specific commands. dd's
[two modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes) —
CLI, and MCP for an MCP client — and its
[commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands) are
documented in its own repository.
