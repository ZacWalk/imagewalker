# Application rules

Properties that hold across all five applications.

- Each is a standalone x64 executable with one entry-point file and one test
  file.
- Debug executables end in `d`; binaries, settings and logs all live in `exe\`.
- Settings use an ini file named after the executable. There is no runtime
  registration and no feature gating.
- All five applications write an executable-specific latest-run log.
  ImageWalker 3.0 writes timestamped UTF-8 diagnostics to that file as well as
  the debugger and an attached console; inability to open its log is reported.
- Image and pixel scratch uses owned containers such as `IW::CBuffer`. Fixed
  stack buffers remain only for small bounded Win32 strings, and in the crash
  reporter, which runs after a fault and must not allocate.
- Background work is an owned work item, and worker code does not send blocking
  messages to the UI thread.
- A worker-completion payload posted to the UI thread carries ownership, and the
  poster reclaims it when the post fails — see
  `CMainFrame::SignalImageLoadComplete` and `SignalSearchingFolder`.
- Every resource script embeds a local manifest, and the application icon is the
  lowest-numbered icon resource.
- The four WTL applications use the shared `iw_flags` settings. ImageWalker 3.0
  keeps its stricter compiler configuration in `src30/CMakeLists.txt`.
- All five applications use `exe\ImageWalker.chm`. ImageWalker 3.0 has its own
  topic set and opens it through the native HTML Help API, not the WTL helper.

Reviewed batch work in ImageWalker 3.0 adds five more. It covers Convert,
Rename and Sync; Copy, Move and Edit saves run to completion instead. The
behaviour these protect is described in
[workflows-3.0.md](workflows-3.0.md).

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
