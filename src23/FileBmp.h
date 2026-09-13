// ImageWalker by Zac Walker
//
// Purpose: Windows BMP and DIB reader/writer.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

class  CLoadBmp : public CLoad<CLoadBmp>
{
protected:
	void CopyPalette(LPCOLORREF pDst, IW::LPCCOLORREF pSrc, UINT nColorEntries) const;

public:
	CLoadBmp();
	virtual ~CLoadBmp();

	static CString _GetKey() { return _T("MicrosoftWindowsBitmap"); };
	static CString _GetTitle() { return App.LoadString(IDS_BMP_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_BMP_DESC); };
	static CString _GetExtensionList() { return _T("BMP,DIB"); };
	static CString _GetExtensionDefault() { return _T("BMP"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::SAVE; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);
};
