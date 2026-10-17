# To do

Open development tasks and their acceptance criteria. Keep completed work out
of this list; record lasting requirements in the relevant development guide.

## Warnings

Follow [warnings.md](warnings.md) and validate with a full Debug and Release
rebuild, not just incremental totals.

- [ ] **MSVC 19.51 diagnostics.** Check the shared native imaging sources in
      `libiw/src/legacy`, `src10/DibMemory.cpp` and the 2.x `UtilMd5.cpp`
      implementations for `C4189`, `C4244`, `C4458` and `C4459`.
      Validate with compiler 19.51.36260 or newer.
      Done when neither configuration emits these families and no suppression
      has been added.

## Build and repository

- [ ] **Release packaging.** Releases need one command that builds Release,
      signs each executable with an RFC 3161 timestamp, and zips each
      application on its own and all five together. Signing uses a local
      hardware-backed certificate, so it cannot run in hosted CI. Done when
      the command produces signed, timestamped zips whose executables pass
      `Get-AuthenticodeSignature`, and [build.md](build.md) documents it.

## Common image engine

- [ ] **One decoder per format.** JPEG, PNG, GIF, TIFF and BMP have a native
      decoder for documents and a Windows codec for BGRA display, so the two
      paths can disagree on damaged input, colour handling and orientation.
      Converge on one, for example by backing native storage with WIC or by
      building libjpeg-turbo with SIMD and decoding for display natively.
      Done when each format has one decoder, its display decode time is
      measured against the current path on the same fixtures, and the backend
      rule in `libiw/src/NativeCodec.h` and its tests are updated.
- [ ] **Exact JPEG auto-rotation at partial edge blocks.** Turning a JPEG
      upright losslessly leaves partial edge blocks in place for EXIF
      orientations 2, 3, 4, 6, 7 and 8 when the width or height is not a
      multiple of the JPEG block size. Done when such photos are shown exactly
      upright without a lossy save, with tests at non-multiple sizes for all
      eight orientations.

## ImageWalker 3.0 features

- [ ] **Printing, contact sheets, search, descriptions and lossless JPEG
      rotation.** Add these capabilities through the 3.0 platform and task
      views, without depending on a WTL application. Preserve unsupported
      metadata, and never substitute recompression for lossless rotation.
      Done when all five capabilities have documented usage and regression
      coverage.

## ImageWalker 3.0 QA findings

User-facing symptoms and workarounds are listed in
[known-issues.md](known-issues.md).

- [ ] **WebP orientation and metadata.** Apply EXIF orientation when opening
      WebP and preserve EXIF, XMP and ICCP chunks when saving. Use libwebp's
      metadata-container support and cover upright decoding, in-place edits
      and metadata round trips with tests.
- [ ] **Cross-format Save as metadata.** Carry camera, date and GPS fields
      when saving a photo into another format, such as HEIC or PNG to JPEG.
      Done when Save as maps EXIF and XMP into the destination format, or
      explicitly warns that it cannot.
- [ ] **Animated GIF and WebP.** Animate both formats in the viewer and full
      screen, report their canvas size and use the first frame as the
      thumbnail. Convert and Edit must either refuse an animated source with
      a specific reason or say that only its first frame will be written.
      Validate viewing and saving with animated fixtures.
- [ ] **Very large images at 100%.** Test a 10000 x 8000 JPEG at 100% zoom,
      including a full decode refused by the 256 MiB limit. Show real pixels
      by decoding the visible region or tiles, or within a safe memory budget;
      otherwise explicitly say that the image is shown reduced. Cover refused
      decodes and repeated zoom requests with regression tests.
