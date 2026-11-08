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
- Empty image copies stay empty, self-assignment preserves pixels and source
  dimensions, and DIB recreation honours the requested bitmap storage mode.
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
- Other native upright saves use libfiles' metadata normalization. If existing
  EXIF or XMP cannot be verified or corrected, encoding fails before publication.
- A carried colour profile must describe RGB, because saved pixels are RGB.
- Image and undo metadata blobs are deep copies wherever mutation is possible.
- A decoded document owns its frames and metadata, and a copy of it is an
  independent mutable snapshot; a `libfiles::pixel_rows` view borrows its storage,
  so keep the owner alive and validate strides and lengths at adapters. Keep
  preview-only document formats out of photo editing even when their thumbnails
  can be decoded.

## Threading and UI

- Worker exceptions are handled per item, so one malformed file cannot stop a
  queue for the rest of the session. Log worker and completion exceptions.
- Escape cancels a choice dialog; it must never accept a destructive alternate
  such as Don't Save or Replace. Folder scan failures are errors, not empty folders.
- Thumbnail identity distinguishes complete shell items even when their hashes
  collide; directory refreshes invalidate outstanding thumbnail work.
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
- Recreated render surfaces reset their cached GDI selection state, and
  rectangle intersections retain only the shared area with nonnegative extents.

## Regression checks

Test one-item worker failure followed by successful work, stop and start reuse,
image conversion and lock boundaries, alpha edits, format round trips, damaged
and misnamed images, temporary-file commit behaviour and rename rollback.
Compressed Undo streams report consumed input bytes and zero-length transfers
consistently. Cover search negation and self-assignment in each 2.x suite. For
ImageWalker 3.0, also
test reviewed writes, identity changes, cancellation, rename cycles and
sidecars, edit previews, saves and backups, all eight orientations, carried and
corrected metadata including malformed EXIF, and Sync collection settings.

Record additional test work in [todo.md](todo.md).
