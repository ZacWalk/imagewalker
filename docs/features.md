# Features

What the five built applications do. Release identity is in
[versions.md](versions.md); the historical feature sets they descend from are in
[history.md](history.md).

## ArtMate 1.0

A read-only shell-folder browser and image viewer. Thumbnail or detail listing
beside an image pane, selection, clipboard copy, fit and fixed zoom, panning,
and a collage of several selected images. Address bar, toolbar and status bar
are individually toggled. It reads BMP, GIF, JPEG, PCX, PNG and TIFF. It has no
editing, batch tools, printing or search, and it writes no image files.

## ImageWalker 2.x

ImageWalker 2.0, 2.2 and 2.3 have the same feature set:

- shell-folder browsing, history, file operations and Explorer integration;
- thumbnail, detail and matrix layouts with configurable sorting;
- Normal, Folders, Search, Edit and Print modes;
- full-screen viewing, image stepping, fit/fill and fixed zoom levels;
- advanced search, including subfolders and image properties;
- non-destructive crop, straighten, horizontal and vertical perspective, and
  colour adjustment, applied on Save or Save As;
- lossless left/right JPEG rotation from the item view;
- image printing, page setup and contact-sheet creation;
- Convert Images, Resize Images and Loss-Less JPEG batch tools;
- multi-file rename with a preview of every destination;
- descriptions with spell checking, and an image-detail display.

The batch tools write to a selected destination folder and refuse to use a
source file as their own output.

The only functional difference between the trees is that ImageWalker 2.3
exposes its read-only Metadata command from the Image menu. It displays General,
Details, IPTC, XMP and EXIF tabs without modifying the loaded image. Everything else that
distinguishes them is presentation.

### Image formats

| Format | Read | Write | Backed by |
|---|:-:|:-:|---|
| JPEG | yes | yes | libjpeg-turbo |
| PNG | yes | yes | libpng, zlib |
| TIFF | yes | yes | libtiff |
| GIF | yes | yes | giflib |
| BMP / DIB | yes | yes | in-tree |
| PSD | yes | no | in-tree |
| PCX | yes | no | in-tree |
| WPG | yes | no | in-tree |
| WMF / EMF | yes | no | GDI |
| Compound documents | thumbnail only | no | in-tree OLE reader |

JPEG carries EXIF and ICC metadata, PNG and TIFF carry alpha and metadata, and
GIF carries transparency and multiple pages. EXIF is read through libexif and
XMP through expat. Registration and file-dialog filters behave identically in
all three trees.

## ImageWalker 3.0

A separate modern application with its own platform layer, canvas, collection
model, settings and tests. It does not use the WTL frame. Its own topic set in
the shared compiled help explains all six file workflows.

A mode strip runs down the left edge — Browse, Folders, Convert, Rename, Edit,
Sync and Full screen — with folder shortcuts beneath it. Pressing the active
mode returns to Browse. The shortcut bar lists the known picture, document,
desktop and download folders plus any folder the user pins; a right click
removes a pinned one.

Browsing offers details, thumbnails and matrix layouts, a photos-only filter,
sorting by name, modified date, type, size, dimensions or media duration, a shell folder tree,
navigation history, a breadcrumb bar, zoom and fit, collage, drawn property
panels, clipboard copy/cut/paste, drag and drop, and delete to the Recycle Bin.

Readable formats are discovered from the installed WIC codecs at startup, so
the set grows with the machine; RAW formats are readable wherever their vendor
codec is installed. Writable formats are PNG, JPEG, BMP, TIFF and bundled
static WebP (lossy or lossless, including alpha). Animated WebP is refused
rather than silently reduced to its first frame. Photo metadata is read through WIC: camera, date
taken, exposure, aperture, ISO and focal length.

Six file workflows sit alongside the browser — Open with, Copy/Move, Convert or
Resize, Batch Rename, Photo Edit and Synchronize. Their behaviour, including
the shared planning, review and collision model, is described in
[workflows-3.0.md](workflows-3.0.md).

Video and audio open in a playback task with play/pause, stop, seek and volume
controls. A native video child surface is separate from the image resampler.
Video thumbnails are poster frames; duration, frame size, frame rate and codec
information come from Media Foundation. Support depends on installed Windows
media components and codecs. Missing Media Foundation is reported when media
is used and does not prevent ordinary image browsing.

Photo Edit adds normalized perspective corners, interactive crop handles,
conservative automatic document detection and draft Undo/Redo. Invalid or
oversized geometry is refused, and failed saves keep the draft.

Current-session file Undo covers reviewed tasks, the Copy/Move dialog, direct
Rename and Photo Edit saves, with conflict checks and retained recovery copies.
It is separate from Windows shell/Recycle Bin recovery; details and limitations
are in [workflows-3.0.md](workflows-3.0.md).

Printing, contact sheets,
search, descriptions and lossless JPEG rotation are 2.x features that 3.0 does
not have. Like the WTL applications, 3.0 keeps a latest-run log beside its
executable; timestamped UTF-8 diagnostics also go to the debugger and an
attached console. Outstanding development is listed in
[todo.md](todo.md).

### 3.0 parity direction

Printing, contact sheets, search, descriptions and lossless JPEG rotation all
belong in the 3.0 product scope. They should use the modern platform and task
surfaces, not introduce a dependency on a WTL application. Metadata changes
must retain unsupported data, and lossless rotation must not silently fall back
to recompression.

This settles the scope decision, not implementation status: those five
capabilities remain available in 2.x until their independent 3.0 implementations
are delivered and validated.
