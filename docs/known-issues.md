# Known issues

Limitations of the current builds, with workarounds.

## ImageWalker 3.0

| Issue | Workaround |
|---|---|
| **Only the first picture is shown for animated GIF, animated PNG and multi-page TIFF.** Animated WebP cannot be opened. Convert and Photo Edit's Save as output only one picture. | View animations or additional pages in another application. In-place Photo Edit refuses to replace multi-page TIFFs and animated PNGs. |
| **Images above about 67 megapixels are not decoded at full size.** A decode is refused above 256 MiB, so 100% zoom draws the reduced preview enlarged, without saying so, and Convert and Photo Edit's Save fail because the photo could not be decoded. | Fit-to-window views are unaffected. Inspect, convert or edit such images in another application. |
| **WebP photos ignore their orientation, and WebP saves keep no metadata.** A WebP stored on its side is shown on its side, and saving to WebP writes no EXIF, XMP or colour profile. | Edit's default original backup keeps the untouched file. |
| **Media trims follow packet and keyframe boundaries.** The range can differ from the selected frames; severely damaged and timeline-discontinuous media are refused. | Check the result using Save As. Use a frame-accurate editor for exact cuts. |
| **Media playback and export do not support identical formats.** Only selected FFmpeg decoders are bundled. AV1 playback needs a compatible Windows codec. | When preview is unavailable, remux a copy if Edit offers that option, or play the file in another application. |

Convert deliberately copies the colour profile but no camera data or GPS
position; see [workflows-3.0.md](workflows-3.0.md#4-convert-or-resize).
Printing, contact sheets, search, descriptions and lossless JPEG rotation are
not available in ImageWalker 3.0; use ImageWalker 2.3 for those functions.

## ImageWalker 1.0 and 2.x

| Issue | Workaround |
|---|---|
| **Some features of the original releases are unavailable.** See the list below. | Use another application for these functions. |
| **Grayscale and black-and-white TIFFs of up to 8 bits stored in strips do not open.** The native TIFF reader refuses them. | Open them in ImageWalker 3.0, which falls back to Windows for unsupported TIFF layouts. |
| **ImageWalker 1.0 opens photos, not embedded PowerPoint or Visio document previews.** | Open the document in its own application. |
| **ImageWalker 2.x: auto-rotated JPEGs can show a strip of unturned blocks along one edge.** When a JPEG's EXIF orientation is applied and its width or height is not a multiple of the JPEG block size, the partial edge blocks stay where they were stored. | Clear **Auto rotate using EXIF orientation** in Options, or turn the file with Tools > Loss-Less Jpeg and **Drop non-transformable edge blocks**. |

These are unavailable in the rebuilt editions:

- Web galleries, the `iw://` protocol and the embedded browser.
- Screen capture, MAPI e-mail and TWAIN/STI scanning.
- DirectShow video thumbnails and COM automation.
- Thumbnail caching, zip browsing, wallpaper and file associations.
- Toolbar and keyboard-shortcut customisation.
- Slide shows, Compare mode, and the Filters and Actions views.
