// ImageWalker by Zac Walker
//
// Purpose: GIF reader/writer, including animation frames and transparency.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"

#ifdef COMPILE_GIF

extern "C"
{

#include "iw/gifcompat.h"

}

class  CLoadGif : public CLoad<CLoadGif>
{
public:

	CLoadGif();
	virtual ~CLoadGif();

	static CString _GetKey() { return _T("CompuServeGraphicsInterchange"); };
	static CString _GetTitle() { return App.LoadString(IDS_GIF_TITLE); };
	static CString _GetDescription() { return App.LoadString(IDS_GIF_DESC); };
	static CString _GetExtensionList() { return _T("GIF"); };
	static CString _GetExtensionDefault() { return _T("GIF"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::SAVE | IW::ImageLoaderFlags::TRANSPARENCY | IW::ImageLoaderFlags::MULTIPAGE | IW::ImageLoaderFlags::HTML; };

	bool Read(const CString &str, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &str, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);
	bool Write(IW::IStreamOut *pStreamOut, IW::IStreamIn *pStreamIn, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);

protected:

	static int ReadFunction(GifFileType *pGifFile, GifByteType *pBytesOut, int nAmount);
	static int WriteFunction(GifFileType *pGifFile, const GifByteType *pBytesIn, int nAmount);

};

#endif // COMPILE_GIF
