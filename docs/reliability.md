# Reliability requirements

Requirements for file safety, image processing, metadata, Win32 contracts,
threading and user interaction.

## File operations

- Encoders write beside the destination through `IW::CFileTemp`; callers commit
  successful output explicitly, and failed work is discarded.
- Batch tools refuse to treat an input file as their own output, and do not
  replace originals implicitly.
- Multi-file rename uses temporary names and a rollback that never overwrites a
  file which already exists.
- Deleting from the browser or item view goes to the Recycle Bin through the
  shell, with the shell's own confirmation. ImageWalker 3.0's Sync is the one
  deliberate exception: it deletes permanently, behind a counted confirmation.

## Image and metadata integrity

- Surface locks validate dimensions, stride and allocation size before pixel
  access.
- Decoders reject input without a complete frame, invalid headers or offsets,
  and truncated pixel data. Damage after a complete GIF frame or TIFF page ends
  the image there with a warning instead of discarding the complete frames.
- Indexed 1-, 2-, 4- and 8-bit pixels round-trip at odd and even widths.
- Alpha pages retain source alpha during edits; composited render lines are not
  written back as source pixels.
- EXIF orientation changes only when pixels are physically reoriented.
- In ImageWalker 3.0, use the orientation WIC reports to decode photos upright.
  Account for the HEIF decoder applying orientation itself; never turn a photo
  twice. Save upright pixels with the orientation reset to 1.
- JPEG APP13 chains and unsupported metadata structures are preserved without
  lossy reconstruction. ImageWalker 3.0 carries a JPEG's metadata segments byte
  for byte, correcting values in place; EXIF it cannot follow is dropped rather
  than left stating the old orientation.
- A carried colour profile must describe RGB, because saved pixels are RGB.
- Image and undo metadata blobs are deep copies wherever mutation is possible.
- Treat `libiw` frame and metadata views as borrowed storage. Keep their document
  alive, validate strides and lengths at adapters, and use `clone()` for an
  independent mutable snapshot. Keep preview-only document formats out of photo
  editing even when their thumbnails can be decoded.

## Threading and UI

- Worker exceptions are handled per item, so one malformed file cannot stop a
  queue for the rest of the session.
- Claimed-item state is released on success, failure, cancellation and stale
  completion.
- Completion handlers continue the request chain even when a result is no longer
  displayable.
- Directory notification failures disarm the watch instead of producing a
  refresh loop.
- UI callbacks use valid ATL/WTL message context; alternate maps use plain
  handlers rather than `_EX` macros.
- Layout and control state update only when values change, limiting repaint and
  flicker during interactive edits.

## Regression checks

Test one-item worker failure followed by successful work, stop and start reuse,
image conversion and lock boundaries, alpha edits, format round trips, damaged
and misnamed images, temporary-file commit behaviour and rename rollback. For
ImageWalker 3.0, also
test reviewed writes, identity changes, cancellation, rename cycles and
sidecars, edit previews, saves and backups, all eight orientations, carried and
corrected metadata including malformed EXIF, and Sync collection settings.

Record additional test work in [todo.md](todo.md).
