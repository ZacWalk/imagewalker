// ImageWalker by Zac Walker
//
// Purpose: Reads images out of an OLE compound document.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

class  CLoadCompoundDoc : public CLoad<CLoadCompoundDoc>
{
public:
	CLoadCompoundDoc();
	virtual ~CLoadCompoundDoc();

	static CString _GetKey() { return _T("CompoundDocFile"); };
	static CString _GetTitle() { return App.LoadString(IDS_CDF_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_CDF_DESC); };
	static CString _GetExtensionList() { return _T("PPS,PPT,VSD"); };
	static CString _GetExtensionDefault() { return _T("PPS"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::THUMBONLY; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);
};
