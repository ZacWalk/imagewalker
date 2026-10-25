// ImageWalker by Zac Walker
//
// Purpose: PCX native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FilePcx.h"
#include "LibfilesDocument.h"

CLoadPcx::CLoadPcx() = default;
CLoadPcx::~CLoadPcx() = default;

bool CLoadPcx::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::pcx,
		GetKey(), input, output, status);
}

bool CLoadPcx::Write(const CString&, IW::IStreamOut*, const IW::Image&,
	const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}
