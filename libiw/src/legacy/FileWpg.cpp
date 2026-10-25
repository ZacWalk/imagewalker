// ImageWalker by Zac Walker
//
// Purpose: WPG native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FileWpg.h"
#include "LibfilesDocument.h"

CLoadWpg::CLoadWpg() = default;
CLoadWpg::~CLoadWpg() = default;

bool CLoadWpg::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::wpg,
		GetKey(), input, output, status);
}

bool CLoadWpg::Write(const CString&, IW::IStreamOut*, const IW::Image&, const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}
