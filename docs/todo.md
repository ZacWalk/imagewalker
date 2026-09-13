# To do

Development this repository still needs. Every item names the evidence, so it
can be picked up without rediscovery, and states what "done" looks like.

## Settled decisions

- [x] Unified 2.x log/test banners, About titles and resource product names on
      the public labels 2.0, 2.2 and 2.3, with matching four-part numeric
      versions. Tests inspect the compiled version resources and real About
      dialogs; the convention is in [versions.md](versions.md).
- [x] Added an executable-specific latest-run UTF-8 log to 3.0. Timestamped,
      serialized records are flushed to disk; a competing writer cannot
      truncate a live log, and open/write failures are surfaced.
- [x] Retained unmodified Delete, F2 and F7 in 2.x. Delete uses the Recycle Bin,
      F2 opens rename, and F7 opens the Move To menu. Compiled accelerators and
      native command/rename workflows are regression-tested.
- [x] Tool configuration retains actionable issues for missing/invalid files,
      unusable declarations, search folders and missing executables. Startup
      status and Help > Tool configuration expose them, and the log retains
      the complete diagnostics.

## Stale comments

- [x] Corrected the shared-header comments to describe four WTL applications,
      with MBCS ArtMate and Unicode 2.x trees, and removed obsolete version and
      platform assumptions in `include/iw/appinfo.h`, `help.h`, `helpids.h`,
      `logfile.h`, `logowindow.h` and `cursorautohide.h`.
- [x] Refreshed all three 2.x `CMakeLists.txt` headers; they no longer describe
      a port in progress or claim removed source files are present.
- [x] Added the historical-release note to `src23/ViewAboutDlg.h`.
- [x] Explained the intentional exclusion of shell ZIP folders in
      `include/iw/shell.h` without describing it as a defect.

## Unfinished code in the 2.x trees

These changes apply to `src20`, `src22` and `src23`.

- [x] Implemented `ViewRenderSurface.h` line drawing and routed `CRender`
      through it. Width, clipping and restored GDI state are regression-tested;
      drawing failures are logged.
- [x] Removed the obsolete thumbnail-cache block in `ViewFolderWindow.h`.
- [x] Retained the existing folder-page thumbnail stretch and removed the
      speculative better-scale note in `ViewFolderLayout.h`.

## Warnings

Policy is in [warnings.md](warnings.md); `.\dd.ps1 test` prints the current
grouped totals. Done, for each family, means the code no longer emits it and no
suppression was added.

- [x] Replaced `CString` variadic arguments with explicit character pointers
      throughout the WTL trees (`C4840`), including ArtMate's GIF statistics.
- [x] Validated `C4189` cleanup. Unused locals have been removed across
      the 2.x trees and shared colour controls, including the final ArtMate
      TIFF and Release-only sites found by the combined build. The final
      Debug/Release rebuilds emit no `C4189`.
- [x] Finished the tracked conversion/shadowing warnings without suppressions.
      Tests cover channel rounding, JFIF density saturation, PSD samples,
      64-bit quantizer moments and print-range limits. Explicit `/EHsc`
      eliminates `C4530` and has an actual cross-stack destructor-unwinding
      regression. The final builds emit none of the tracked warning families.
- [x] Moved assignments out of conditions (`C4706`), including palette
      realisation and the ArtMate GIF sub-block reader.

## Tests

- [x] Added ArtMate tests for all six loaders using generated in-memory
      fixtures: decoded pixels, dimensions, palettes, alpha, interlacing,
      TIFF byte orders, JPEG colour/grayscale, malformed-input recovery and
      BMP thumbnail scaling. Fixed GIF and PCX failure reporting exposed by
      those tests. Both build configurations pass.
- [x] Added shared 2.x tests for command routing, compiled accelerators, real
      rename-dialog review controls, pagination and selection, print-page
      pixels/DC state, and PNG contact sheets that preserve existing output.
      Test states start with empty folders instead of enumerating Pictures;
      the contact-sheet copy path now initialises its text-height field.
- [x] Added 3.0 ModeBar input/DPI/reentrancy tests and real MainWindow/TaskView
      switching, fullscreen, toolbar, progress and cancellation tests using
      recording platform adapters. Both build configurations pass.
- [x] Added concrete MainFrame dispatch, modal rename execution/cancellation,
      contact-sheet options and partial-page flushing, and native HWND
      startup/fullscreen coverage. PDF spooler tests target only the explicitly
      verified Microsoft PDF driver and wait for completed, exclusively readable
      output; timeout cleanup cancels only the owned job.

## Help

- [x] Retired unused `HELP_WALLPAPER` and `HELP_ACQUIRE` aliases and definitions;
      their numbers remain reserved. Strict help validation passes.
- [x] Added `Help/metadata.html` for ImageWalker 2.3, linked from Versions,
      Descriptions, contents and index. Corrected the old claim that the
      read-only dialog edits metadata. Strict help validation passes.
- [x] Added seven 3.0 topics covering its six file workflows, exposed through
      Help > User guide / F1. All five targets build the shared CHM; 3.0 uses
      its own topic set and native HTML Help entry point.

## Build and repository

- [x] Removed unused openjpeg and sqlite recipes, aliases, aggregate-target
      dependencies and the sqlite adapter. Neither appears in the regenerated
      build target list; no application dependency was removed.
- [ ] Verify the first hosted CI run. `.github/workflows/ci.yml` now checks the
      toolchain, runs full Debug/Release tests and GUI smoke tests, validates
      help and retains diagnostics on `windows-2025-vs2026`. The workflow has
      not yet been pushed or run on GitHub. Publishing was not approved while
      the user was unavailable, so this remains the one blocked checklist item.
- [x] Added `vs-debug` and `vs-release` CTest presets matching the existing
      build configurations, with driver-contract coverage of their pairing.
- [x] Retired the unused NSIS recipe. Distribution is deliberately portable
      in a writable folder; historical installer artwork remains.
- [x] Made header ownership visible: the three shared utility headers live in
      `include/core`, 3.0 owns `src30/util_color.h`, and the legacy colour
      control lives in `include/iw/ColorButton.h`. Include paths/call sites and
      [layout.md](layout.md) match the new locations.
- [x] Added `.editorconfig`, `.clang-format` and `.clang-format-ignore`.
      The validated C++ profile preserves include order and uses the existing
      tab/Allman conventions; vendored and generated code is excluded. This
      does not impose a whole-tree reformat or a CI formatting gate.

The maintenance and regression-test changes above pass `.\dd.ps1 test`:
all six CTest entries and all five GUI smoke checks in both Debug and Release.
Native audio/video transport is also exercised with
`IW30_TEST_NATIVE_PLAYBACK=1`, including paused seek, mute, pause/resume and
stop. `.\dd.ps1 check-help --yes` passes. Hosted CI remains unverified.

## ImageWalker 3.0 features

Behaviour that already exists is described in
[workflows-3.0.md](workflows-3.0.md).

- [x] **Video playback.** Added a playback task, native video child surface,
      poster thumbnails and transport. Live frames do not reuse image-resampler
      tokens. Media Foundation entry points are optional, checked runtime loads,
      so Windows N without the feature pack can still browse images.
- [x] **Audio playback.** The shared native transport plays audio with volume,
      pause/resume, seek and stop. Real muted-audio playback is tested.
- [x] **Media metadata.** Added duration, dimensions, frame rate, codec and
      duration sorting, with malformed-input and known-fixture tests.
- [x] **Parity scope decision.** All five capabilities — printing, contact
      sheets, search, descriptions and lossless JPEG rotation — belong in
      3.0's scope, as recorded in [features.md](features.md). This is a scope
      decision only; their independent 3.0 implementations are not yet delivered.
- [x] **Configured tools and multi-selection.** `{item-paths}` passes every
      selected path, individually quoted. Mixed types require a tool matching
      every item. Existing one-file templates retain explicit first-item-only
      labels; registered applications still require one extension.
- [x] **WebP.** Bundled libwebp provides static lossy/lossless decoding and
      encoding with alpha independently of WIC extensions. Transactional saves,
      refusals, collisions and exact lossless round trips are tested; animation
      is refused instead of silently losing frames.
- [x] **Photo Edit.** Added inverse-homography perspective correction,
      conservative document detection, crop/corner dragging and draft Undo/Redo.
      Tests cover alpha, rotations, invalid geometry, allocation-amplification
      limits and preview/save geometry consistency.
- [x] **Undo.** Current-session file Undo covers reviewed tasks, Copy/Move,
      direct Rename and Photo Edit saves. Protected originals/manifests persist
      for manual recovery without automatic eviction. Tests cover overlapping
      renames, external edits, partial transfers, folder identity rebasing and
      node-only directory ownership. Windows shell/Recycle Bin operations and
      external tools use their own recovery, as documented in
      [workflows-3.0.md](workflows-3.0.md).
