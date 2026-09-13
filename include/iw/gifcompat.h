#pragma once

// giflib 4.1 -> 5.2 API changes, kept here so LoadGif.cpp stays as it was written.
// The open/close calls gained a trailing error-code argument, and the allocator
// helpers were prefixed. Macros are not re-expanded inside their own expansion,
// so the names below still resolve to the real giflib functions.

#include <gif_lib.h>

#define DGifOpen(userPtr, readFunc)       DGifOpen((userPtr), (readFunc), NULL)
#define EGifOpen(userPtr, writeFunc)      EGifOpen((userPtr), (writeFunc), NULL)
#define DGifCloseFile(gifFile)            DGifCloseFile((gifFile), NULL)
#define EGifCloseFile(gifFile)            EGifCloseFile((gifFile), NULL)

#define MakeMapObject                     GifMakeMapObject
#define FreeMapObject                     GifFreeMapObject
#define EGifPutBlob                       EGifPutExtensionBlock

// giflib 5 reports errors per-file instead of through a global.
inline int GifLastError() { return 0; }
