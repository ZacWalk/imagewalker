# ImageWalker agent notes

ImageWalker builds five standalone x64 applications from `src10`, `src20`,
`src22`, `src23` and `src30`. The three 2.x trees share one feature set and keep
separate presentation profiles. ArtMate 1.0 and ImageWalker 3.0 are independent
implementations.

## Reference documents

Start with [README.md](README.md), which indexes every document. The ones that
constrain a code change most often:

| Document | Read before |
|---|---|
| [docs/app-rules.md](docs/app-rules.md) | Any change to settings, logging, workers or resources |
| [docs/porting.md](docs/porting.md) | Any change to the WTL trees |
| [docs/reliability.md](docs/reliability.md) | Any change to file writes, pixels, metadata or threading |
| [docs/workflows-3.0.md](docs/workflows-3.0.md) | Any change to the ImageWalker 3.0 task views |
| [docs/build.md](docs/build.md) | Any change to CMake, presets, tests, help or dependencies |
| [docs/todo.md](docs/todo.md) | Picking up outstanding work |

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

- Shared code in `include/` is header-only. There is no shared compiled library.
- WTL `_EX` message macros are not used in alternate message maps.
- Floating-point calculations do not call the integer-only `IW::Min`, `IW::Max`
  or `IW::Clamp` helpers.
- Pointer-sized Win32 values stay pointer-sized through callbacks, messages and
  window data.
- Temporary output is committed explicitly with `CFileTemp::Close`; destruction
  aborts incomplete output. Rollback never overwrites an existing file.
- Pixel storage tags change only when the corresponding pixels change, and
  metadata that cannot be represented losslessly is kept as opaque bytes.
- Alpha-bearing pages are read with `GetLine`; `RenderLine` is for compositing.
- Worker exception handling stays inside the processing loop, so one malformed
  file cannot retire a worker, and completion paths always arrange the next
  request — including stale-result and cancellation paths.
- A worker-completion payload posted to the UI thread carries ownership, and the
  poster reclaims it when the post fails.
- In Convert, Rename and Sync, a reviewed plan is immutable once analysed, a
  stale generation is dropped rather than merged, Cancel is partial completion
  rather than rollback, and a destination appearing between Review and Run is
  never silently overwritten.
- A configured external tool is launched only from a discovered executable path.
- Resource edits preserve the app icon as the lowest-numbered icon resource, and
  both Debug and Release are compiled by the test command.

## Validation

Validate source edits with the narrowest relevant test, then run `.\dd.ps1 test`.
Validate help edits with `.\dd.ps1 check-help --yes`, which runs the link checker
and the strict help build described in [docs/build.md](docs/build.md).

## dd modes

dd has two modes, and the command line selects between them.

**CLI mode** is the default and the single behavior owner. Each verb runs once,
prints one schema 1 result envelope and exits:

```pwsh
pwsh -NoProfile -File ./dd.ps1 test --json
pwsh -NoProfile -File ./dd.ps1 build debug
```

**MCP mode** starts with `dd mcp`. The process becomes a stdio JSON-RPC server and
stays alive until stdin closes, adapting typed MCP requests onto CLI mode — each
tool call runs as a child `dd` invocation and returns that command's envelope.

MCP mode owns stdout for protocol messages, so it prints no result envelope,
rejects `--json`, and sends diagnostics to stderr. Its workspace boundary is the
project root. Register the client configuration with `dd ide --mcp`; add
`--allow-execution` only when project-code execution is intended.
