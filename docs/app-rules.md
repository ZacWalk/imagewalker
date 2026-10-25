# Application rules

Development requirements for all five applications.

- Each is a standalone x64 executable with one entry-point file and one test
  file.
- Link `libiw` statically for image loading and manipulation. Keep UI,
  resources, settings, worker scheduling and final file publication in the
  application. See [libiw-plan.md](libiw-plan.md).
- Debug executables end in `d`; binaries, settings and logs all live in `exe\`.
- Keep version resources in each application's directory: `ArtMate.rc` for
  ArtMate, `Ver.h` and `ImageWalker.rc` for 2.x, and `app.rc` for 3.0.
  Use `ArtMate 1.0` and `1.0.0.0` in ArtMate's version resource and test the
  product name against its application title. Follow
  [architecture-2x.md](architecture-2x.md#version-labels) for 2.x labels.
- Settings use an ini file named after the executable. There is no runtime
  registration and no feature gating.
- All five applications write an executable-specific latest-run log. A second
  instance, finding that log held by the first, writes `<exe>-<pid>.log` beside
  it rather than truncating it. ImageWalker 3.0 writes timestamped UTF-8
  diagnostics to that file as well as the debugger and an attached console;
  inability to open either log is reported.
- Image and pixel scratch uses owned containers such as `IW::CBuffer`. Fixed
  stack buffers remain only for small bounded Win32 strings, and in the crash
  reporter, which runs after a fault and must not allocate.
- Background work is an owned work item, and worker code does not send blocking
  messages to the UI thread. ImageWalker 3.0 joins its work queues at exit, so
  an item that loops over several things polls `platform::exit_token()`
  between them; otherwise a closed app runs on until the batch ends.
- A worker-completion payload posted to the UI thread carries ownership, and the
  poster reclaims it when the post fails — see
  `CMainFrame::SignalImageLoadComplete` and `SignalSearchingFolder`.
- Every resource script embeds a local manifest, and the application icon is the
  lowest-numbered icon resource.
- The four WTL applications use the shared `iw_flags` settings. ImageWalker 3.0
  keeps its stricter compiler configuration in `src30/CMakeLists.txt`.
- All five applications embed the compiled help and open it from an unmarked
  copy that `include/core/help_file.h` writes to the user's temporary folder,
  never from beside the executable: HTML Help shows blank pages for a `.chm`
  extracted from a downloaded zip or opened from a network share. ImageWalker
  3.0 has its own topic set and opens it through the native HTML Help API, not
  the WTL helper.

Apply the following safety rules to reviewed batch work in ImageWalker 3.0:
Convert, Rename and Sync. Copy, Move and Edit saves run to completion instead.
See [workflows-3.0.md](workflows-3.0.md) for usage.

- A plan is immutable once analysed. A change to a source, destination, format
  or collision policy bumps the generation and discards it.
- A completion carrying a stale generation is dropped, never merged into a newer
  plan.
- Cancel is partial completion, never rollback. Every row keeps an outcome.
- No plan writes two sources to one destination, and a destination that appears
  between Review and Run is never silently overwritten.
- A configured external tool is launched only from an executable path discovered
  on disk, never from text in the configuration file.
- File-task Undo secures originals before destructive writes and records only
  successful mutations. It never authorizes later external changes by refreshing
  an untouched path's fingerprint. Recovery copies are not automatically evicted.
