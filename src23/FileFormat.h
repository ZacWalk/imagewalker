// ImageWalker by Zac Walker
//
// Purpose: CLoadBase: what every image format implements - read, write
//          and its settings panel.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#define COMPILE_GIF
#define COMPILE_PNG
#define COMPILE_TIFF
#define COMPILE_PSD

class  CLoadBase  : public IW::IImageLoader
{
public:
	
	// Construct destruct
	CLoadBase();
	virtual ~CLoadBase();
	
	virtual bool Read(const CString &strType, IW::IStreamIn *pStreamIn, IW::IImageStream *pImageOut, IW::IStatus *pStatus) = 0;
	virtual bool Write(const CString &strType, IW::IStreamOut *pStreamOut, const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus) = 0;

	HWND CreateSettingsWindow(HWND /*hWndParent*/, IW::CodecSettings & /*settings*/) { return nullptr; };
};

template<class T>
class  CLoad : public CLoadBase
{
public:
	CString GetKey() const { return T::_GetKey(); };
	CString GetTitle() const { return T::_GetTitle(); };
};


inline unsigned short ReadMSBShort(IW::IStreamIn *pStream) 
{ 
	unsigned short n = 0;
	pStream->Read(&n, sizeof(n), NULL);

	return _byteswap_ushort(n);
};

inline unsigned long ReadMSBLong(IW::IStreamIn *pStream) 
{ 
	unsigned long n = 0;
	pStream->Read(&n, sizeof(n), NULL);

	return _byteswap_ulong(n);
};

typedef int Quantum;
typedef int IndexPacket;
typedef DWORD PixelPacket;

const int MaxTextExtent = 300;
const int MaxRGB = 255;

inline BYTE ScaleCharToQuantum(BYTE bb) { return bb; };

inline int ReadBlobLSBLong(IW::IStreamIn *pStreamIn)
{
	int value;

	DWORD dw;
	if (!pStreamIn->Read(&value, 4, &dw) || (dw != 4))
	{
		throw IW::invalid_file();
	}

	return value;
}


inline unsigned short ReadBlobLSBShort(IW::IStreamIn *pStreamIn)
{
	unsigned short value;

	DWORD dw;
	if (!pStreamIn->Read(&value, 2, &dw) || (dw != 2))
	{
		throw IW::invalid_file();
	}

	return value;
}



inline BYTE ReadBlobByte(IW::IStreamIn *pStreamIn)
{
	BYTE b;
	DWORD dw;
	if (!pStreamIn->Read(&b, 1, &dw) || (dw != 1))
	{
		throw IW::invalid_file();
	}

	return b;
}

