# History

ImageWalker was a Windows image browser and viewer written by Zac Walker
between 1998 and 2006. This repository preserves the released versions as
buildable, runnable 64-bit applications, and adds a modern successor.

The guiding idea was stated plainly at the time and still applies: life is too
short for slow software. Windows Explorer generated thumbnails slowly, and the
image suites of the period were heavy. ImageWalker was native C++ against the
Win32 API, and its job was to make a folder of photographs open immediately.

## Releases

| Year | Release | What it introduced |
|---:|---|---|
| 1998 | ArtMate 1.0 | The first version, released before the ImageWalker name. A thumbnail browser and image viewer, with no editing, batch tools, printing or searching. |
| 2001 | ImageWalker 2.0 | The first release under the ImageWalker name: thumbnail browsing, descriptions, print preview and a slide show. |
| 2003 | ImageWalker 2.2 | The widest feature set of any version: Normal, Compare and Print modes, the Filters and Actions views, and six batch tools. The most widely distributed release. |
| 2006 | ImageWalker 2.3 | Image tagging, EXIF auto-rotation and the Resize Images wizard. The last 2.x release. |
| 2026 | ImageWalker 3.0 | A new implementation against the modern Windows SDK, sharing no code with 2.x. |

Each release picked a song from the year it shipped, and each About box still
links to it. See [versions.md](versions.md).

## What the applications became

The 2.x applications were shareware. They are now free software under the MIT
licence, with no registration, reminder screen or feature limit, and their
settings live beside the executable rather than in the registry.

The port to x64 kept the original WTL structure and the release presentations,
but did not carry everything forward. These were part of the original 2.x
product and are deliberately absent from the trees here:

- the web gallery and its template parser;
- the `iw://` pluggable protocol and the embedded browser;
- screen capture, MAPI e-mail, and TWAIN/STI scanning;
- DirectShow video thumbnails and the COM automation server;
- the thumbnail cache, zip browsing, wallpaper and file associations;
- toolbar and accelerator customisation;
- the slide show, and the Compare mode, Filters and Actions surfaces named in
  the 2.2 About text.

They depended on components that Windows has since withdrawn, on licensed
third-party code, or on install-time system integration that a portable
application should not perform. [porting.md](porting.md) records the constraints
that remain, and [features.md](features.md) describes what the applications do
now.

## Afterwards

ImageWalker was superseded by [Diffractor](https://diffractor.com/), which
carries the same performance-first approach into video, audio and RAW, and is
itself open source. Former ImageWalker registrations were converted into
Diffractor sponsorships.

ImageWalker 3.0 in this repository is not Diffractor. It is a smaller, separate
application that revisits the ImageWalker idea on a modern SDK. Its capabilities
are described in [features.md](features.md).
