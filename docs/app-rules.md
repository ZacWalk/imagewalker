# Application rules

Rules for all five applications.

- Each is a standalone x86 or x64 executable with its own entry point and an
  application test suite covering its identity, layout, settings and command
  availability.
- Keep UI, resources, settings, worker scheduling and final file publication
  in the application. Use the shared imaging headers in 2.x and 3.0.
  ImageWalker 1.0 uses libfiles and the Windows SDK directly.
  See [imaging.md](imaging.md).
- Debug executables end in `d`; binaries, settings and logs all live in `exe\`.
- Version identity lives in each application's directory: `ImageWalker.rc` for
  1.0, `Ver.h` and `ImageWalker.rc` for 2.x, and the `libdesktop_add_app` call
  in `src30/CMakeLists.txt` for 3.0. 1.0's version resource uses
  `ImageWalker 1.0` and `1.0.0.0`, and its tests check the product name against
  the application title; [architecture-2x.md](architecture-2x.md#version-labels)
  gives the 2.x labels.
- Settings use an ini file named after the executable. There is no runtime
  registration and no feature gating.
- Each application writes an executable-specific latest-run log. A second
  instance, finding that log held by the first, writes `<exe>-<pid>.log` beside
  it rather than truncating it. ImageWalker 3.0 writes timestamped UTF-8
  diagnostics to that file, the debugger and an attached console, and reports
  when it cannot open its log.
- Image and pixel scratch uses owned containers such as `IW::CBuffer`. Fixed
  stack buffers remain only for small bounded Win32 strings, and in the crash
  reporter, which runs after a fault and must not allocate.
- Background work is an owned work item, and worker code sends no blocking
  messages to the UI thread. ImageWalker 3.0 joins its work queues at exit, so
  an item that loops over several things polls `platform::exit_token()` between
  them; otherwise a closed application runs on until the batch ends.
- A worker-completion payload posted to the UI thread carries ownership, and
  the poster reclaims it when the post fails, as the 2.x
  `CMainFrame::SignalImageLoadComplete` and `SignalSearchingFolder` do.
- The 1.0 and 2.x resource scripts embed their own `app.manifest`;
  `libdesktop_add_app` generates 3.0's manifest and version resource. Each
  application's icon is its lowest-numbered icon resource.
- All five embed the compiled help and open it from the copy described in
  [build.md](build.md#help). ImageWalker 3.0 has its own topics and opens them
  through the HTML Help API, not the WTL helper.
- Preserve ImageWalker 1.0's read-only scope and its smaller frame and views.
  Read through libfiles' public codec registry rather than implementing codecs.
- Keep ImageWalker 3.0 changes in its own platform, canvas, collection and task
  interfaces, with no dependency on a WTL application.

## ImageWalker 3.0 file tasks

These rules govern Convert, Rename and Sync, the reviewed batch tasks; Copy,
Move and Edit saves run to completion instead.
[workflows-3.0.md](workflows-3.0.md) describes their use.

- A plan is immutable once analysed. A change to a source, destination, format
  or collision policy bumps the generation and discards it.
- A completion carrying a stale generation is dropped, never merged into a
  newer plan.
- Cancel is partial completion, never rollback. Every row keeps an outcome.
- No plan writes two sources to one destination, and a destination that appears
  between Review and Run is never silently overwritten.
- A configured external tool is launched only from an executable path
  discovered on disk, never from text in the configuration file.
- File-task Undo secures originals before destructive writes and records only
  successful mutations. It never authorizes later external changes by
  refreshing an untouched path's fingerprint, and never evicts recovery copies
  automatically.
