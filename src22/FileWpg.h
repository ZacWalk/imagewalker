// ImageWalker by Zac Walker
//
// Purpose: WordPerfect Graphics reader.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

class  CLoadWpg : public CLoad<CLoadWpg>
{
public:
	CLoadWpg();
	virtual ~CLoadWpg();

	static CString _GetKey() { return _T("WordPerfectGraphic"); };
	static CString _GetTitle() { return App.LoadString(IDS_WPG_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_WPG_DESC); };
	static CString _GetExtensionList() { return _T("WPG"); };
	static CString _GetExtensionDefault() { return _T("WPG"); };
	static DWORD _GetFlags() { return 0; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);
};
