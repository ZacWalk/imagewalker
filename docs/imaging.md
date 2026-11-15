# Shared imaging

All five applications use one set of application-owned imaging headers under
`include/iw` and link libfiles and libdesktop directly. There is no shared
imaging library, DLL, object library or implementation translation unit.
Release identity, interface, settings and workflows stay in the application
directories.

## Ownership

```text
Application UI and workflow
    -> shared application imaging adapters
        -> libfiles readers / native encoders / metadata
        -> libdesktop/Windows adapters where required
```

| Location | Responsibility |
|---|---|
| [Modern headers](../include/iw/imaging) | Standard C++ image, document, codec and manipulation contracts |
| [Modern implementation details](../include/iw/imaging/detail) | Inline libdesktop and libfiles adapters; image processing stays in the public headers |
| [Historical headers](../include/iw/legacy) | Inline 2.x types, stream and storage adapters, metadata presentation and historical algorithms |
| [Regression consumer](../tests/imaging) | Shared imaging cases and the ownership and dependency check |
| [Shared regression definitions](../include/iw/imagingtests.h) | Engine cases also run by the 2.x application suites |

Consume the `iw_imaging` and `iw_legacy` interface targets declared in
[imagewalker-imaging.cmake](../cmake/imagewalker-imaging.cmake). Historical
consumers include [Legacy.h](../include/iw/legacy/Legacy.h), which composes the
compatibility models and their inline implementations. Keep shared definitions
inline and shared state in inline variables; never include an implementation
`.cpp` file or add a one-definition implementation macro.

Modern headers include no application precompiled header, read no application
globals and open no application dialogs, and `include/iw/imaging` stays free of
ATL, WTL, Win32 handles, `TCHAR` and intrinsics. Historical headers and sink
adapters may keep the ATL and WTL contracts the 2.x releases need. The
[boundary check](../tests/imaging/check-boundary.cmake) rejects a compiled
imaging library, sources under `src/shared`, headers under `libiw/`, a modern
header that includes a historical, platform or `.cpp` file, an application
header that only forwards to a shared one, a native codec writer in a
historical adapter, and in 3.0 a private Win32 frame or drawing implementation
or a native control window.

Keep resource loading, drawing into application canvases, clipboard and OLE
objects, printing and contact-sheet layout in the applications, and Media
Foundation playback and poster acquisition in [src30](../src30). Reusable
platform pixel primitives belong in libdesktop; application editing and
historical storage algorithms stay inline here.

## Choosing an image representation

Use [PixelImage](../include/iw/imaging/Pixel.h) for owned, top-down,
straight-alpha BGRA pixels. Its original dimensions describe the source
independently of a reduced decode: treat a failed or reduced decode as such,
and never take a preview's enlarged pixels as a full-resolution result.

Use [Document.h](../include/iw/imaging/Document.h)'s `decode_document` when
native pixel storage, multiple pages, palettes or retained metadata matter.
Native formats decode through libfiles' `decode_document` straight into a small
application policy sink that owns one `libfiles::decoded_document`, never
through `IW::Image` and back. The sink enforces the retained-frame budget
before each allocation and forwards cancellation and progress. The document
owns every frame's rows - top-down and DWORD aligned, with their storage,
palette, offsets and animation attributes - and every metadata block libfiles
names, so a copy is an independent snapshot. Beside it the sink keeps what
ImageWalker's loaders add: the orientation, the stored size a thumbnail was
scaled from, and the loader's description and name. A document is not just its
first frame.

Read stored rows as BGRA with libfiles' `copy_bgra`, over a frame's `rows()` or
a `libfiles::pixel_rows` view of any storage, such as a page or a bottom-up
DIB. It writes straight into the adapter's buffer, with no full-size
intermediate, and `pixels_from_rows` and `frame_pixels` make a `PixelImage`. A
view borrows its storage: keep the owner alive, and validate every extent,
stride and destination size. Palette entries are BGRA with alpha, including
those handled by helpers named for `COLORREF`.

Pixel-format tags, alpha and composited pixels follow
[reliability.md](reliability.md#image-and-metadata-integrity).

## Decoding

[Codec.h](../include/iw/imaging/Codec.h) provides path and memory decoding,
capability queries, dimensions, orientation, photo properties and encoding.

- Recognise extensions through the shared registry, and keep an image distinct
  from a preview-only document: decoding a PowerPoint preview does not make the
  document an editable photo.
- Use the native document path for packed and indexed storage, multiple pages
  and retained metadata, and the BGRA path for display, editing and the formats
  only Windows decodes.
- Choose a BGRA decoder only through `pixel_decoder` in Codec.h: libfiles for
  WebP, identified by signature for encoded bytes and by extension for a path;
  a Windows codec for PNG, TIFF, any extension a Windows codec claims and
  anything the native decoders cannot read; otherwise the native decoders.
  Native-storage decodes start with the native loaders and reach WebP and the
  Windows codecs only through their own loaders, which call one backend
  directly and never re-enter selection. A display decode reads the loader's
  image straight to BGRA, without a document's copy of its rows.
- JPEG, PNG, GIF, TIFF and BMP therefore have two decoders - libfiles on the
  document path, Windows on the BGRA path - which can disagree on damaged
  input, colour handling and orientation. Windows providers may stay where
  their capabilities differ, but never as application-version forks; both
  paths share codec ownership, pixel utilities, colour processing and CPU
  detection.
- Native JPEG, PNG, GIF, TIFF, PCX, PSD, WPG and uncompressed BMP readers call
  libfiles; the historical document adapter adapts rows, palettes, metadata and
  diagnostics, not file formats. Memory streams lend an immutable `GetMemory()`
  view for the synchronous decode; files keep positional buffered I/O.
- Identify input by its extension, then by a complete signature. A first word
  shared with an image format, as RIFF audio and OLE containers share one, does
  not make a file an image: report it as unsupported, not damaged.
- Pass thumbnail bounds, orientation policy, memory limits, cancellation and
  progress explicitly. A modern decode request defaults to a 256 MiB budget;
  the ImageWalker 1.0 adapter and the historical loaders keep a 1 GiB native
  page ceiling. The policy sink counts frame pixels, palettes and metadata
  before allocating, independently of libfiles' working limit.
- Reject input without one complete frame, with invalid headers or offsets, or
  with truncated pixel data, and never report partially initialized pixels as
  an image. Damage after a complete GIF frame or TIFF page ends the document
  there with a warning, a GIF without its trailer is complete, and
  informational header fields such as BMP `bfSize` are not validated. PSD
  planes and PackBits rows must be complete, never leaving an uninitialized
  tail. Keep the native colour policies separate from display gamma
  conversion.
- Check `OperationResult` and `DecodeResult`. Distinguish invalid input,
  unsupported input, decoding or encoding failure, resource limits and
  cancellation, and keep warnings rather than replacing them with a
  successful-looking default.
- Decode photos upright through libdesktop's image loader, which applies the
  orientation Windows reports after a container's own transforms - the HEIF
  decoder turns its images itself - so no photo is turned twice. Native
  orientation handling supports all eight EXIF values and resets the tag only
  after turning the pixels.
- Read photo properties - camera, capture date, exposure - through libfiles'
  `read_properties`, in every format libfiles scans; the 2.x date taken follows
  the same date rule. A capture date is the camera's clock time, whatever
  offset the file states for it.

## Encoding and metadata

`encode_to_file()` writes only the temporary output its caller owns. It does
not choose a final name, replace a destination, create backups or commit a
reviewed task; those policies stay in the applications:

- 2.x commits explicitly through
  [CFileTemp](../include/iw/legacy/FileStreams.h).
- 3.0 keeps temporary-output publication, identity checks, collision policy,
  backups and Undo in [Files.cpp](../src30/Files.cpp) and its task services.
- ImageWalker 1.0 is read-only.

Historical save adapters turn their `IW::Image` into one
`libfiles::decoded_document` at the legacy boundary and call `encode_document`.
The native encoder owns indexed, packed and multipage storage and opaque
metadata; an unsupported storage layout is reported, not flattened to lossy
BGRA. Keep only the explicit `libdesktop::core` surface adapter and the
compressed DIB, metafile and OLE compatibility paths local, and never add a
second native JPEG, PNG, BMP, GIF or TIFF encoder. libfiles encodes static
WebP, exact under transparent pixels.

Pass the metadata policy per operation: 3.0's Convert carries only the colour
profile, while Edit carries all of the source's metadata. A common engine is
not permission to erase that difference. Read metadata through libfiles
without decoding a raster; a format with no native document decoder can still
supply scanned blocks, with a warning that says so. Attach a profile only when
its header declares RGB, because saved pixels are RGB, and leave orientation
and dimension corrections in EXIF and XMP to libfiles rather than editing them
in an application adapter. A JPEG-to-JPEG save captures the source's marker
header through the same handle and applies `carry_jpeg_metadata` after
encoding, so markers that are not recognised survive. Historical metadata
adapters keep Author, PNG text and Photoshop resources as distinct opaque kinds
through native round trips.

Use [JpegTransform.h](../include/iw/imaging/JpegTransform.h) for
coefficient-only JPEG operations. Keep failure and cancellation distinct from
"not JPEG", never turn a failed lossless operation into recompression, keep
opaque marker segments, and never trim incomplete edge blocks silently. The
[2.x batch adapters](../src23/ImagingTransform.h) own stream closure and
commit, not coefficient processing.

## Manipulation and application adapters

Use the common quarter-turn mapping, pixel conversion, colour transform,
histograms, auto adjustments and runtime SIMD dispatch. Use
[ImageEdits.h](../include/iw/imaging/ImageEdits.h),
[ImageResampler.h](../include/iw/imaging/ImageResampler.h) and
[EditGeometry.h](../include/iw/imaging/EditGeometry.h) for BGRA edits,
alpha-aware resizing and perspective geometry.

Keep distinct resampling filters and geometry parameterizations where they
represent different requested operations: the 2.x fixed-frame perspective
controls and 3.0's source quadrilateral are different input models, not
reasons to duplicate colour processing or format readers. Map UI values at the
adapter, and add no release-number switches to the engine.

| Application boundary | Keep local |
|---|---|
| [1.0 loader](../src10/LoadAny.cpp) | MBCS-to-Unicode paths, DIB storage adaptation, resource lookup, progress and error presentation, stored orientation and the 1 GiB native page budget |
| [1.0 scaling](../src10/DibExtra.cpp) | DIB conversion and display-buffer ownership; call the shared resize operation |
| [2.x streams](../src23/ImagingStreams.h) | Snapshot application preferences and search policy into an image request |
| [2.x file adapter](../src23/FileFormatAny.cpp) and [JPEG panel](../src23/FileJpeg.cpp) | File opening and publication, and encoder settings UI |
| [2.x metadata presentation](../src23/Metadata.cpp) | Property-list controls and application resources |
| [3.0 files](../src30/Files.cpp) | Folder enumeration, media dispatch, diagnostics and safe publication |
| [3.0 edit session](../src30/ImageEditor.cpp), [cache](../src30/ImageResampler.cpp) and [crop interaction](../src30/EditGeometry.cpp) | Undo history, preview and cache lifetimes, and pointer interaction |

Adapters are translation and ownership code: put no second codec, tone
algorithm or image-format registry there. Keep the compatibility include
directory private to the 2.x targets; 1.0 and 3.0 use the standard C++ API.
Include modern contracts as `iw/imaging/...` and historical contracts as
`iw/legacy/...`; an application directory holds no forwarding header.

## Work and diagnostics

Operations run synchronously on the application's worker; the shared code owns
no work queue. Completion payloads, stale generations, UI dispatch and
scheduling stay in the applications.

Each application installs its diagnostic sink with `set_diagnostic_sink` from
[Diagnostics.h](../include/iw/imaging/Diagnostics.h); without one, diagnostics
go to stderr. Never include an application's `TCHAR`-dependent logger in the
shared headers: they also serve the MBCS ImageWalker 1.0.

## Validation

```powershell
.\dd.ps1 test --label imagewalker-imaging
.\dd.ps1 test --app iw23
.\dd.ps1 test
```

The standalone suite exercises the public API and the shared native tests
without an application module or resources, and the header test links several
translation units to check shared state and the one-definition rule. The shared
checks carry every application's label, so a focused application run includes
them; see [build.md](build.md#tests).

Maintain coverage for:

- All stored pixel formats, odd-width indices, palette alpha and row order.
- PNG, GIF and BMP round trips, grayscale and CMYK JPEG, TIFF tiles and
  malformed input.
- Damage after complete GIF frames and TIFF pages, BMP header tolerance,
  signature-only identification and fallback loaders that cannot recurse.
- Exact rotations, crop, alpha-preserving edits, resampling and SIMD agreement.
- All eight orientations, corrected metadata and independent copies.
- Lossless JPEG markers, coefficient round trips, cancellation and memory
  limits.

Compare performance on the same fixtures and hardware when changing an
algorithm or adapter layout, measuring both time and peak memory, and avoid a
full-image copy merely to cross a library boundary. Lossless operations require
exact pixels; set explicit tolerances before changing lossy decoding or
encoding.

Sharing the engine does not add animation, printing, lossless JPEG commands or
any other workflow to a release. A limitation stays in
[known-issues.md](known-issues.md) until a separately tested change resolves
it.
