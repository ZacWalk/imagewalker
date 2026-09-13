# ImageWalker 2.x architecture

ImageWalker 2.0, 2.2 and 2.3 are parallel standalone builds of one application
shape. They share the browsing, search, editing, printing, format and batch-tool
behaviour described in [features.md](features.md), while preserving the
presentation of each release.

The three trees deliberately contain their own translation units. Common code
that is naturally header-only lives in `include/`; there is no shared binary
library and no runtime dependency between the applications. The compiled source
list is identical in all three `CMakeLists.txt` files, and the shared feature
set is catalogued in [features.md](features.md).

The trees share their command structure, modes, item and image models, edit
stack, batch tools, printing, loader registry, settings, logging and worker
ownership model. Only the presentation below differs, plus the Metadata command
that 2.3 alone exposes from the Image menu.

## Presentation profiles

| Version | Preserved presentation |
|---|---|
| 2.0 | Three toolbar rows, wordmark logo, tab mode selector, framed panes, system colours |
| 2.2 | Three toolbar rows, wordmark logo, vertical mode bar, favourites area, framed panes, system colours |
| 2.3 | Mode buttons in the toolbars, borderless split panes, skinned chrome, dark palette, orange selection |

The 2.0 and 2.2 image panes open zoom navigation only from the small gutter
toolbar button, never as an overlay on the image. Their scroll bars appear only
when the scaled image overflows the corresponding viewport axis; full screen
keeps the zoom buttons but hides both scroll bars.

All three profiles use the current monitor's full screen rectangle, without
window borders or empty toolbar padding. Leaving full screen restores the
previous window placement and maximized state; 2.3 also restores its window skin.

Resource identity, icons, version strings, About text and the layout code
implementing these profiles stay local to each tree. The structural divergences
are small: 2.0 and 2.2 have `ViewDescriptionWindow.h` where 2.3 has
`ViewDescriptionDlg.h`, `ViewModeTabs.h` is 2.0's tab mode selector, and
`ViewTabCtrl.h` belongs only to 2.3.

The single functional divergence is the Metadata command, which only 2.3 exposes
from the Image menu.

## ArtMate 1.0 and ImageWalker 3.0
ArtMate shares `include/iw` and the compiled help with the 2.x trees, but has
its own smaller frame, view and loader set.

ImageWalker 3.0 shares none of this. It is a separate implementation with its
own platform layer, canvas, collection model, task surface, settings and tests,
and its own compiler configuration in `src30/CMakeLists.txt`.
