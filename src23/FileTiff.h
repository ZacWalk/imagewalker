// ImageWalker by Zac Walker
//
// Purpose: TIFF reader/writer over libtiff, including multi-page files.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

#ifdef COMPILE_TIFF

#ifdef unix
#undef unix
#endif
#ifdef __unix
#undef __unix
#endif

#undef int16

#include <tiffio.h>

class  CLoadTiff :  public CLoad<CLoadTiff>
{
private:
	TIFF *m_hTiffOut;

public:

	CLoadTiff();
	virtual ~CLoadTiff();

	static CString _GetKey() { return _T("TaggedImageFile"); };
	static CString _GetTitle() { return App.LoadString(IDS_TIFF_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_TIFF_DESC); };
	static CString _GetExtensionList() { return _T("TIF,TIFF"); };
	static CString _GetExtensionDefault() { return _T("TIF"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::ALPHA | IW::ImageLoaderFlags::METADATA | IW::ImageLoaderFlags::SAVE; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);

	bool AddMetaDataBlob(const IW::MetaData &data);
};


#endif // COMPILE_TIFF
