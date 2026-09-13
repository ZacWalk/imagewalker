// ImageWalker by Zac Walker
//
// Purpose: Windows metafile and enhanced metafile reader; plays the metafile
//          into a bitmap.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

class  CLoadMetaFile : public CLoad<CLoadMetaFile>
{
public:

	CLoadMetaFile();
	virtual ~CLoadMetaFile();
		
	static CString _GetKey() { return _T("WindowsMetaFile"); };
	static CString _GetTitle() { return App.LoadString(IDS_WMF_TITLE); }; 
	static CString _GetDescription() { return App.LoadString(IDS_WMF_DESC); };
	static CString _GetExtensionList() { return _T("WMF,EMF"); };
	static CString _GetExtensionDefault() { return _T("EMF"); };
	static DWORD _GetFlags() { return 0; };

	bool Read(LPCBYTE pByte, DWORD nSize, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);

private:

	int ReadEnhancedMetaFile(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize);
	int ReadMetaFilePict(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize);
	int ReadMetaFile(IW::IImageStream* pImageOut, LPCBYTE pByte, DWORD nSize);
};
