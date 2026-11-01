// ImageWalker by Zac Walker
//
// Purpose: The unit tests, run by passing /test. Covers the loaders, the
//          pixel pipeline, the edit stack and the metadata readers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"

#include "ImagingStreams.h"
#include "iw/legacy/FilePng.h"
#include "iw/legacy/FileBmp.h"
#include "iw/legacy/FileGif.h"
#include "FileJpeg.h"
#include "iw/legacy/FileTiff.h"
#include "iw/legacy/FilePcx.h"
#include "iw/legacy/FilePsd.h"
#include "iw/legacy/FileWpg.h"
#include "iw/legacy/FileFormatList.h"
#include "FileFormatAny.h"
#include "ImagingFilter.h"
#include "iw/legacy/ImagingEdits.h"
#include "ImagingTransform.h"
#include "iw/legacy/MetadataExif.h"
#include "iw/legacy/ImagingBlitter.h"
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
#include "iw/classicviewtests.h"

// ImageWalker 2.0 -- run with /test.

#include "iw/imagingtests.h"
