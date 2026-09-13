// ImageWalker by Zac Walker
//
// Purpose: ZSoft PCX reader/writer.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

class  CLoadPcx : public CLoad<CLoadPcx>
{
public:

	CLoadPcx();
	virtual ~CLoadPcx();

	static CString _GetKey() { return _T("ZSoftPaintbrush"); };
	static CString _GetTitle() { return App.LoadString(IDS_PCX_TITLE); };
	static CString _GetDescription() { return App.LoadString(IDS_PCX_DESC); };
	static CString _GetExtensionList() { return _T("PCX"); };
	static CString _GetExtensionDefault() { return _T("PCX"); };
	static DWORD _GetFlags() { return 0; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);

private:

	// Runs are allowed to span scanlines, so the remainder carries between rows.
	bool DecodeRow(LPCBYTE &p, LPCBYTE pEnd, LPBYTE pOut, int nCount);

	int _nRepCount;
	BYTE _nRepByte;
};
