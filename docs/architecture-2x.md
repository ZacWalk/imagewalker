# ImageWalker 2.x development guidelines

Follow these rules when changing ImageWalker 2.0, 2.2 or 2.3.
[app-rules.md](app-rules.md) holds the rules for all five applications.

- Keep each application independently buildable, with its own UI translation
  units and header-only shared imaging, and no runtime dependency on another
  ImageWalker executable.
- Put reusable UI helpers, image algorithms and adapters inline in `include/`,
  and use libfiles for formats, documents, codecs and metadata; see
  [imaging.md](imaging.md).
- Fix common imaging behaviour once, in the shared headers. Apply equivalent
  UI, tool, printing, settings, logging and worker-ownership fixes consistently
  across `src20`, `src22` and `src23`.
- Keep each release's resources and interface details local to its directory.

## Interface conventions

| Version | Interface to preserve |
|---|---|
| 2.0 | Three toolbar rows, wordmark logo, tab mode selector, framed panes, system colours |
| 2.2 | Three toolbar rows, wordmark logo, vertical mode bar, favourites area, framed panes, system colours |
| 2.3 | Mode buttons in the toolbars, borderless split panes, skinned chrome, dark palette, orange selection |

In 2.0 and 2.2, open zoom navigation only from the small gutter toolbar button,
never as an overlay on the image. Show scroll bars only when the scaled image
overflows the corresponding viewport axis; keep the zoom buttons but hide both
scroll bars in full screen.

Use the current monitor's full screen rectangle without window borders or empty
toolbar padding. On leaving full screen, restore the previous window placement
and maximized state; in 2.3, also restore the window skin.

Keep resource identity, icons, version strings, About text and layout code
local to each application. Preserve each release's description view:
`ViewDescriptionWindow.h` in 2.0 and 2.2, `ViewDescriptionDlg.h` in 2.3.
Keep `ViewModeTabs.h` with 2.0's tab selector and `ViewTabCtrl.h` with 2.3.
Keep the read-only Metadata command in ImageWalker 2.3's Image menu.

## Version labels

Use `ImageWalker 2.0`, `ImageWalker 2.2` and `ImageWalker 2.3` in log banners,
test banners, About titles and resource product names, and the numeric file and
product versions `2.0.0.0`, `2.2.0.0` and `2.3.0.0`. Take each public label from
`APP_VERSION_NAME` in that application's `Ver.h`, and test it against the
compiled version resource and About dialog.

Keep Delete mapped to the Recycle Bin, F2 to Rename and F7 to Move To, and
validate the compiled accelerator tables, not just the resource source.
