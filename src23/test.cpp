// ImageWalker by Zac Walker
//
// Purpose: The unit tests, run by passing /test. Covers the loaders, the
//          pixel pipeline, the edit stack and the metadata readers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"

#include "ImagingStreams.h"
#include "iw/imaging/FilePng.h"
#include "iw/imaging/FileBmp.h"
#include "iw/imaging/FileGif.h"
#include "FileJpeg.h"
#include "iw/imaging/FileTiff.h"
#include "iw/imaging/FilePcx.h"
#include "iw/imaging/FilePsd.h"
#include "iw/imaging/FileWpg.h"
#include "iw/imaging/FileFormatList.h"
#include "FileFormatAny.h"
#include "ImagingFilter.h"
#include "iw/imaging/ImagingEdits.h"
#include "ImagingTransform.h"
#include "iw/imaging/MetadataExif.h"
#include "iw/imaging/ImagingBlitter.h"
#include "ViewRenderSurface.h"
#include "ViewRenameDlg.h"
#include "ViewAboutDlg.h"

#include "iw/commontests.h"
#include "iw/rendertests.h"

#include "AppCommands.h"
#include "ViewModelState.h"
#include "ToolContactSheet.h"
#include "iw/legacyworkflowtests.h"
#include "iw/legacyboundarytests.h"
#include "iw/legacynativeworkflowtests.h"

// ImageWalker 2.3 -- run with /test.

#include "iw/imagingtests.h"
