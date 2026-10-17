# To do

Open development tasks and their acceptance criteria. Keep completed work out
of this list; record lasting requirements in the relevant development guide.

## Warnings

Follow [warnings.md](warnings.md) and validate with a full Debug and Release
rebuild, not just incremental totals.

- [ ] **MSVC 19.51 diagnostics.** Resolve `C4189` at `Imaging.cpp(180)` and
      `ImagingConvert.cpp(94)` in all three 2.x trees; `C4244` at
      `src10/DibMemory.cpp(81)`; `C4458` at `src10/LoadTiff.cpp(154)` and
      `UtilMd5.cpp(130)` in the 2.x trees; and `C4459` at
      `src10/LoadJpg.cpp(95)`. Validate with compiler 19.51.36260 or newer.
      Done when neither configuration emits these families and no suppression
      has been added.

## Build and repository

- [ ] **Release packaging.** Releases need one command that builds Release,
      signs each executable with an RFC 3161 timestamp, and zips each
      application on its own and all five together. Signing uses a local
      hardware-backed certificate, so it cannot run in hosted CI. Done when
      the command produces signed, timestamped zips whose executables pass
      `Get-AuthenticodeSignature`, and [build.md](build.md) documents it.

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
