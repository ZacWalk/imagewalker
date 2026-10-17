# libiw architecture and integration

Use `libiw` as the common image engine for all five applications. Keep release
identity, interface, settings and workflows in the application directories.
The library is statically linked: each application remains a standalone x64
executable, with no `libiw.dll` or dependency on another ImageWalker executable.

## Ownership and dependency direction

```text
Application UI and workflow
    -> application adapters
        -> libiw
            -> bundled codecs / Windows imaging
```

| Location | Responsibility |
|---|---|
| [Public headers](../libiw/include/libiw) | Standard C++ image, document, codec and manipulation contracts |
| [Implementations](../libiw/src) | WIC/WebP, metadata, pixel operations, editing, resampling and CPU dispatch |
| [Native-page implementation](../libiw/src/legacy) | Packed/indexed and multi-page images, bundled native decoders/encoders, metadata and lossless JPEG processing |
| [Compatibility headers](../libiw/compat/iw/imaging) | Existing 2.x types and stream interfaces backed by the shared implementation |
| [Library tests](../libiw/tests) | Application-independent API and native-engine regression tests |
| [Shared regression definitions](../include/iw/imagingtests.h) | Engine cases also exercised by the three 2.x application suites |

Build the single production target `libiw`, exposed as `iw::libiw`. Do not
include an application header or precompiled header from the library. Do not
link an application, inherit `iw::flags`, read application globals or open
application dialogs. Keep the public API free of ATL, WTL, Win32 handles and
`TCHAR`. The native-page backend may use ATL internally; it does not need WTL.

Keep resource loading, drawing into application canvases, clipboard/OLE objects,
printing and contact-sheet layout with the applications. Pure raster operations
belong in the library. Keep Media Foundation playback and poster acquisition in
[src30](../src30).

## Choosing an image representation

Use [PixelImage](../libiw/include/libiw/Pixel.h) for owned, top-down, straight-alpha
BGRA pixels. Original dimensions describe the source independently of a reduced
decode. Treat a failed or reduced decode as such; do not use a preview's enlarged
pixels as evidence that a full-resolution request succeeded.

Use [Document](../libiw/include/libiw/Document.h) when native pixel storage,
multiple pages, palettes or opaque metadata matter. Its frame views describe
format, stride, row order, offsets and animation attributes. Metadata views
retain their type and original bytes. A document is not just its first frame.

Frame and metadata views borrow their document's storage. Keep that owner alive,
and use `clone()` for independent storage. Native pixel rows are DWORD aligned;
validate every borrowed extent, stride and destination size. `copy_bgra()` and
`copy_frame_bgra()` can write directly into an adapter's owned buffer without an
extra full-size intermediate. Indexed palette entries are BGRA, including alpha;
the old `COLORREF` helper names must not be mistaken for a different byte order.

Keep pixel-format tags consistent with actual storage. For example, colouring a
grayscale page may require RGB output. Never write composited display pixels
back as source alpha pixels. Follow [reliability.md](reliability.md).

## Loading, capabilities and encoding

[Codec.h](../libiw/include/libiw/Codec.h) provides path and memory decoding,
capability queries, dimensions/orientation, photo properties and encoding.

- Use the shared registry for extension recognition. Preserve the distinction
  between an image and a preview-only document: being able to decode a PowerPoint
  preview must not make the document an editable photo.
- Use the native document path for packed/indexed storage, multiple pages and
  retained metadata. Use the BGRA path for display/editing and installed Windows
  formats. Bundled WebP does not require a machine-installed WIC extension.
- Select a BGRA decoder only through the rule in
  [NativeCodec.h](../libiw/src/NativeCodec.h): bundled libwebp for WebP,
  identified by signature for encoded bytes and by extension for a path; an
  installed Windows codec where one claims the extension; otherwise the native
  decoders. Native-storage decodes start with the native loaders and reach
  libwebp and WIC only through the WebP and Windows-codec loaders, which call
  one backend directly and never re-enter selection.
- Identify input by its extension, then by a complete signature. A first word
  shared with an image format, as RIFF audio and OLE containers share one, does
  not make a file an image; report it as unsupported rather than damaged.
- Keep native and Windows providers where their capabilities differ, not as
  application-version forks. The native stream interface and the BGRA interface
  have different storage contracts; they share codec ownership, pixel utilities,
  colour processing and CPU detection.
- Pass thumbnail bounds, orientation policy, memory limits, cancellation and
  progress explicitly. The public decode request defaults to a 256 MiB decoded
  storage budget. Reject input without one complete frame, with invalid headers
  or offsets, or with truncated pixel data; never report partially initialized
  pixels as an image. Damage after a complete GIF frame or TIFF page ends the
  document there with a warning, a GIF without its trailer is complete, and
  informational header fields such as BMP `bfSize` are not validated.
- Check `OperationResult`/`DecodeResult`. Distinguish invalid input, unsupported
  input, decoding/encoding failure, resource limits and cancellation. Preserve
  warnings rather than replacing them with a successful-looking default.
- Let WIC own its intrinsic container orientation, then apply only the remaining
  orientation it reports. This avoids turning HEIF twice. Native orientation
  handling supports all eight EXIF values and resets the tag only after changing
  the corresponding pixels.

Run operations synchronously on the application's worker. The library owns no
application work queue. Keep completion payloads, stale generations, UI dispatch
and scheduling in their existing application layers.

The application installs its log callback through
[Diagnostics.h](../libiw/include/libiw/Diagnostics.h); standalone clients receive
diagnostics on stderr when no callback is installed. Never include the
application's `TCHAR`-dependent logger in the backend: the same library also
serves the MBCS ArtMate executable.

### Write ownership

`encode_to_file()` writes only the temporary output owned by its caller. It
does not choose a final name, replace a destination, create backups or commit a
reviewed task. Keep those policies in the applications:

- 2.x commits explicitly through
  [CFileTemp](../libiw/compat/iw/imaging/FileStreams.h).
- 3.0 keeps temporary-output publication, identity checks, collision policy,
  backups and Undo in [Files.cpp](../src30/Files.cpp) and its task services.
- ArtMate remains read-only.

Pass metadata policy per operation. 3.0 Convert intentionally omits descriptive
and GPS metadata, whereas Edit can carry it. A common engine is not permission
to erase this workflow distinction.

Use [JpegTransform.h](../libiw/include/libiw/JpegTransform.h) for coefficient-only
JPEG operations. Keep failure/cancellation distinct from "not JPEG"; never turn a
failed lossless operation into recompression. Preserve opaque marker segments and
do not trim incomplete edge blocks silently. The
[2.x batch adapters](../src23/ImagingTransform.h) own stream closure and commit,
not coefficient processing.

## Manipulation and application adapters

Use the common quarter-turn mapping, pixel conversion, colour transform,
histograms, auto adjustments and runtime SIMD dispatch. Use
[ImageEdits.h](../libiw/include/libiw/ImageEdits.h),
[ImageResampler.h](../libiw/include/libiw/ImageResampler.h) and
[EditGeometry.h](../libiw/include/libiw/EditGeometry.h) for BGRA edits,
alpha-aware resize and perspective geometry.

Keep distinct resampling filters and geometry parameterizations when they
represent different requested operations. For example, the 2.x fixed-frame
perspective controls and 3.0's source quadrilateral are different input models,
not reasons to duplicate colour processing or format readers. Map UI values at
the adapter; do not add release-number switches to the engine.

| Application boundary | Keep local |
|---|---|
| [ArtMate loader](../src10/LoadAny.cpp) | MBCS-to-Unicode paths, DIB storage adaptation, resource lookup, progress/error presentation, stored orientation and the 1 GiB native page budget |
| [ArtMate scaling](../src10/DibExtra.cpp) | DIB conversion and display-buffer ownership; call the shared resize operation |
| [2.x streams](../src23/ImagingStreams.h) | Snapshot application preferences and search policy into an image request |
| [2.x file adapter](../src23/FileFormatAny.cpp) and [JPEG panel](../src23/FileJpeg.cpp) | File opening/publication and encoder settings UI |
| [2.x metadata presentation](../src23/Metadata.cpp) | Property-list controls and application resources |
| [3.0 files](../src30/Files.cpp) | Folder enumeration, media dispatch, diagnostics and safe publication |
| [3.0 edit session](../src30/ImageEditor.cpp), [cache](../src30/ImageResampler.cpp) and [crop interaction](../src30/EditGeometry.cpp) | Undo history, preview/cache lifetimes and pointer interaction |

Treat adapters as translation and ownership code. Do not put a second codec,
tone algorithm or image-format registry there. Keep the compatibility include
directory private to the 2.x targets; 3.0 and ArtMate use the standard C++ API.
Include `iw/imaging/...` or `<libiw/...>` directly; an application directory
holds no forwarding header.

## Build and validation

Keep dependency recipes, versions and hashes in [third-party](../third-party).
Keep the pinned [dd.ps1](../dd.ps1) and [.dd](../.dd) runtime unmodified.
The library is part of both normal presets and of each application target's
dependency graph; it is not a sixth GUI product.

Use matching static CRT settings. Compile modern library code strictly, and
isolate `/permissive` to native compatibility sources. Compile codec callbacks
with `/EHs`, not an overriding `/EHsc`. Preserve optimized cross-translation-unit
inlining: keep LTCG on and replace RelWithDebInfo's `/Ob1` with `/Ob2` in the
library and 2.x directories, or the native line converters stay out of line.
Keep AVX2 instructions confined to their dispatched source.
See [build.md](build.md) and [warnings.md](warnings.md).

```powershell
.\dd.ps1 test --label libiw
.\dd.ps1 test --app iw23
.\dd.ps1 test
```

The standalone suite exercises the public API and shared native tests without
an application module or resources. The
[boundary check](../libiw/tests/check-boundary.cmake) rejects application/WTL
dependencies, legacy types in public headers, reintroduced implementation
copies and application forwarding headers. Both shared checks carry every
application label, so a focused app run also exercises them.

Maintain coverage for:

- All stored pixel formats, odd-width indices, palette alpha and row order.
- PNG/GIF/BMP round trips, grayscale/CMYK JPEG, TIFF tiles and malformed inputs.
- Damage after complete GIF frames and TIFF pages, BMP header tolerance,
  signature-only identification and fallback loaders that cannot recurse.
- Exact rotations, crop, alpha-preserving edits, resampling and SIMD agreement.
- All eight orientations, corrected metadata and independent clones.
- Lossless JPEG markers, coefficient round trips, cancellation and memory limits.
- A failed item followed by successful work and application completion ownership.
- Temporary-output failure, reviewed collisions, backups and explicit commit.
- Application identity, layout, settings and command availability.

Compare performance on the same fixtures and hardware when changing algorithms
or adapter layouts. Measure both time and peak memory, and avoid a full-image
copy merely to cross a library boundary. Exact lossless operations require exact
pixels; establish explicit tolerances before changing lossy decoding or encoding.

Keep animation, large-image and metadata limitations visible in
[known-issues.md](known-issues.md) until a separately tested change resolves them.
Sharing the engine does not itself add animation UI, printing, lossless JPEG
commands or other workflows to a release. Validate help changes with
`.\dd.ps1 check-help --yes`.
