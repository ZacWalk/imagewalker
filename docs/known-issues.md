# Known issues

Limitations and workarounds. Acceptance criteria for resolving the ImageWalker
3.0 issues are in [todo.md](todo.md#imagewalker-30-qa-findings).

## ImageWalker 3.0

| Issue | Workaround |
|---|---|
| **Animated GIF and WebP do not animate.** An animated GIF shows only its first frame, with nothing to say there are more. An animated WebP cannot be opened: it reports "This image could not be read" and 0 x 0 pixels. | View animations in another application. |
| **Very large images at 100% show an enlarged preview.** A decode is refused above 256 MiB, about 67 megapixels, so 100% zoom draws the reduced preview enlarged, without saying so. | Fit-to-window views are unaffected. Inspect such images at full size in another application. |
| **WebP photos ignore their orientation, and WebP saves keep no metadata.** A WebP stored on its side is shown on its side, and saving to WebP writes no EXIF, XMP or colour profile. | Edit's default original backup keeps the untouched file. |
| **Save as into another format keeps only the colour profile.** Saving an edit as a different format, such as HEIC to JPEG, writes no camera, date or GPS fields. | Save in the photo's own format, which keeps all of its metadata. |

Convert deliberately copies the colour profile but no camera data or GPS
position; see [workflows-3.0.md](workflows-3.0.md#4-convert-or-resize).
Printing, contact sheets, search, descriptions and lossless JPEG rotation are
not available in ImageWalker 3.0; use ImageWalker 2.3 for those functions.

## ArtMate 1.0 and ImageWalker 2.x

| Issue | Workaround |
|---|---|
| **Historical release summaries include unavailable features.** For example, the ImageWalker 2.2 About box names Compare mode and six batch tools, which are not all available in the x64 edition. | Use the bundled help for instructions on available commands. |
| **Some historical features are unavailable in the x64 editions.** See the list below. | Use another application for these functions. |

- Web galleries, the `iw://` protocol and the embedded browser.
- Screen capture, MAPI e-mail and TWAIN/STI scanning.
- DirectShow video thumbnails and COM automation.
- Thumbnail caching, zip browsing, wallpaper and file associations.
- Toolbar and keyboard-shortcut customisation.
- Slide shows, Compare mode, and the Filters and Actions views.
