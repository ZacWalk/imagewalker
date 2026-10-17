// ImageWalker by Zac Walker
//
// Purpose: Photoshop PSD reader - the composite image and its resource
//          blocks.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

#ifdef COMPILE_PSD

class  CLoadPsd : public CLoad<CLoadPsd>
{
public:

	CLoadPsd();
	virtual ~CLoadPsd();

	static CString _GetKey() { return _T("PhotoShop"); };
	static CString _GetTitle() { return App.LoadString(IDS_PSD_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_PSD_DESC); };
	static CString _GetExtensionList() { return _T("PSD"); };
	static CString _GetExtensionDefault() { return _T("PSD"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::ALPHA | IW::ImageLoaderFlags::METADATA; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);

	bool AddMetaDataBlob(IW::MetaData &data);
};

#endif // COMPILE_PSD
