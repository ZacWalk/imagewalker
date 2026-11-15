# Reliability requirements

Requirements for file safety, image and metadata integrity, threading and user
interaction.

## File operations

- Encoders write beside the destination through `IW::CFileTemp`. A caller
  commits successful output explicitly with `Close`; destruction aborts
  incomplete output, and failed work is discarded.
- Batch tools refuse to treat an input file as their own output, and do not
  replace originals implicitly.
- Multi-file rename uses temporary names and a rollback that never overwrites a
  file that already exists.
- Deleting from the browser or item view goes to the Recycle Bin through the
  shell, with the shell's own confirmation. ImageWalker 3.0's Sync is the one
  deliberate exception: it deletes permanently, behind a counted confirmation.

## Image and metadata integrity

- Surface locks validate dimensions, stride and allocation size before pixel
  access.
- Empty image copies stay empty, self-assignment preserves pixels and source
  dimensions, and DIB recreation honours the requested bitmap storage mode.
- Decoders reject incomplete and damaged input as
  [imaging.md](imaging.md#decoding) describes, keeping the complete frames of a
  GIF or TIFF damaged after them.
- Indexed 1-, 2-, 4- and 8-bit pixels round-trip at odd and even widths.
- Pixel storage tags match the stored pixels and change only when those pixels
  change; colouring a grayscale page, for example, may require RGB output.
- Alpha pages keep their source alpha through edits: read them with `GetLine`,
  and keep `RenderLine`, which composites, out of anything written back as
  source pixels.
- EXIF orientation changes only when pixels are physically reoriented.
  ImageWalker 3.0 saves upright pixels with the orientation reset to 1; see
  [imaging.md](imaging.md#decoding) for decoding photos upright.
- Metadata that cannot be represented losslessly, such as JPEG APP13 chains and
  unsupported structures, is kept as opaque bytes, never reconstructed lossily.
  ImageWalker 3.0 carries a JPEG's metadata segments byte for byte, correcting
  values in place; EXIF it cannot follow is dropped rather than left stating
  the old orientation.
- Other native upright saves use libfiles' metadata normalization. If existing
  EXIF or XMP cannot be verified or corrected, encoding fails before
  publication.
- Image and undo metadata blobs are deep copies wherever mutation is possible.

## Threading and UI

- Worker exceptions are handled per item, inside the processing loop, so one
  malformed file cannot stop a worker for the rest of the session. Log worker
  and completion exceptions.
- Completion handlers always arrange the next request, including when a result
  is stale, cancelled or no longer displayable.
- Claimed-item state is released on success, failure, cancellation and stale
  completion.
- Escape cancels a choice dialog; it never accepts a destructive alternative
  such as Don't Save or Replace. A folder scan failure is an error, not an
  empty folder.
- Thumbnail identity distinguishes complete shell items even when their hashes
  collide, and a directory refresh invalidates outstanding thumbnail work.
- A failed directory notification disarms the watch instead of producing a
  refresh loop.
- Layout and control state update only when values change, limiting repaint and
  flicker during interactive edits.
- Recreated render surfaces reset their cached GDI selection state, and
  rectangle intersections keep only the shared area, with nonnegative extents.

## Regression checks

Test one-item worker failure followed by successful work, stop and start reuse,
image conversion and lock boundaries, alpha edits, format round trips, damaged
and misnamed images, temporary-file commit behaviour and rename rollback.
Compressed Undo streams report consumed input bytes and zero-length transfers
consistently. Cover search negation and self-assignment in each 2.x suite. For
ImageWalker 3.0, also test reviewed writes, identity changes, cancellation,
rename cycles and sidecars, edit previews, saves and backups, all eight
orientations, carried and corrected metadata including malformed EXIF, and
Sync collection settings.
