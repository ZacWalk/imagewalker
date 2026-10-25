// ImageWalker by Zac Walker
//
// Purpose: PSD native image-stream adapter to libfiles.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#include "LegacyPch.h"
#include "FilePsd.h"
#include "LibfilesDocument.h"

#ifdef COMPILE_PSD
CLoadPsd::CLoadPsd() = default;
CLoadPsd::~CLoadPsd() = default;

bool CLoadPsd::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::psd,
		GetKey(), input, output, status);
}

bool CLoadPsd::Write(const CString&, IW::IStreamOut*, const IW::Image&, const IW::CodecSettings&, IW::IStatus*)
{
	return false;
}

bool CLoadPsd::AddMetaDataBlob(IW::MetaData&) { return true; }
#endif
