# Known issues

Limitations of the current builds, with workarounds.

## ImageWalker 3.0

| Issue | Workaround |
|---|---|
| **Animated GIF and WebP do not animate.** An animated GIF shows only its first frame, with nothing to say there are more, and Convert and Edit write only that frame. An animated WebP cannot be opened: it reports "This image could not be read" and 0 x 0 pixels. | View animations in another application. |
| **Images above about 67 megapixels are not decoded at full size.** A decode is refused above 256 MiB, so 100% zoom draws the reduced preview enlarged, without saying so, and Convert and Photo Edit's Save fail because the photo could not be decoded. | Fit-to-window views are unaffected. Inspect, convert or edit such images in another application. |
| **WebP photos ignore their orientation, and WebP saves keep no metadata.** A WebP stored on its side is shown on its side, and saving to WebP writes no EXIF, XMP or colour profile. | Edit's default original backup keeps the untouched file. |

Convert deliberately copies the colour profile but no camera data or GPS
position; see [workflows-3.0.md](workflows-3.0.md#4-convert-or-resize).
Printing, contact sheets, search, descriptions and lossless JPEG rotation are
not available in ImageWalker 3.0; use ImageWalker 2.3 for those functions.

## ImageWalker 1.0 and 2.x

| Issue | Workaround |
|---|---|
| **Release descriptions name unavailable features.** For example, the ImageWalker 2.2 About box names Compare mode and six batch tools, which are not all available in the x64 build. | Use the bundled help for instructions on available commands. |
| **Some features of the original releases are unavailable.** See the list below. | Use another application for these functions. |
| **Grayscale and black-and-white TIFFs of up to 8 bits stored in strips do not open.** The native TIFF reader refuses them. | Open them in ImageWalker 3.0, which reads TIFF through Windows. |
| **ImageWalker 2.x: auto-rotated JPEGs can show a strip of unturned blocks along one edge.** When a JPEG's EXIF orientation is applied and its width or height is not a multiple of the JPEG block size, the partial edge blocks stay where they were stored. | Clear **Auto rotate using EXIF orientation** in Options, or turn the file with Tools > Loss-Less Jpeg and **Drop non-transformable edge blocks**. |

These are unavailable in the x64 builds:

- Web galleries, the `iw://` protocol and the embedded browser.
- Screen capture, MAPI e-mail and TWAIN/STI scanning.
- DirectShow video thumbnails and COM automation.
- Thumbnail caching, zip browsing, wallpaper and file associations.
- Toolbar and keyboard-shortcut customisation.
- Slide shows, Compare mode, and the Filters and Actions views.
