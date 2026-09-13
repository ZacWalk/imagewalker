# Versions

Five applications, each built from its own directory and each carrying the
identity of the release it preserves. The history behind them is in
[history.md](history.md).

| Tree | Application | Year | Identity |
|---|---|---:|---|
| `src10` | ArtMate 1.0 | 1998 | Read-only thumbnail browser and viewer |
| `src20` | ImageWalker 2.0 | 2001 | First ImageWalker presentation |
| `src22` | ImageWalker 2.2 | 2003 | Vertical mode-bar presentation |
| `src23` | ImageWalker 2.3 | 2006 | Dark, skinned final 2.x presentation |
| `src30` | ImageWalker 3.0 | 2026 | Separate modern implementation |

The three 2.x directories carry one common feature set in three period
presentations. Frame layout, pane treatment, mode selector, palette, logo,
icon, version strings and About box preserve the identity of each release; see
[architecture-2x.md](architecture-2x.md) for what is shared and what is not.

ArtMate is a smaller read-only application. ImageWalker 3.0 has its own
architecture and shares no WTL application code. Its independent help topics
are packaged alongside the historical topics in the common compiled help file.

## About boxes

Each About box names its version, its release year and its theme song.

| Application | Theme song |
|---|---|
| ArtMate 1.0 | "It's Like That" by Run-DMC vs. Jason Nevins |
| ImageWalker 2.0 | "It Wasn't Me" by Shaggy |
| ImageWalker 2.2 | "Where Is the Love?" by The Black Eyed Peas |
| ImageWalker 2.3 | "Crazy" by Gnarls Barkley |
| ImageWalker 3.0 | "Ordinary" by Alex Warren |

The descriptive paragraph in a 2.x About box describes the original release,
not the binary in `exe\`. The 2.2 box, for example, names Compare mode and six
batch tools; those belonged to the 2003 product and are not in this tree. What
the built applications actually do is in [features.md](features.md).

Version numbers live in each application directory: `Ver.h` for the 2.x trees,
`app.rc` for 3.0.

The 2.x log banners, test banners, About titles and resource product names use
the public labels `ImageWalker 2.0`, `ImageWalker 2.2` and `ImageWalker 2.3`.
The corresponding numeric file/product versions are `2.0.0.0`, `2.2.0.0` and
`2.3.0.0`. Each tree's `APP_VERSION_NAME` supplies its public label; regression
tests check that label against the compiled version resource and About dialog.

The historical 2.x file shortcuts are retained: Delete invokes the Recycle Bin
operation, F2 opens rename, and F7 opens the Move To menu. These remain distinct
from ImageWalker 3.0's task shortcuts. Regression tests inspect the compiled
accelerator tables rather than relying only on resource-source text.
