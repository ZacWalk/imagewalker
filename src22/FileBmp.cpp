// ImageWalker by Zac Walker
//
// Purpose: Windows BMP and DIB implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

#include "FileBmp.h"
#include "ImagingStreams.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadBmp::CLoadBmp()
{
}

CLoadBmp::~CLoadBmp()
{
}


void CLoadBmp::CopyPalette(LPCOLORREF pDst, IW::LPCCOLORREF pSrc, UINT uColorEntries) const
{
	for (UINT i = 0; i < uColorEntries; i++)
	{
		pDst[i] = pSrc[i] | 0xff000000;
	}
}


bool CLoadBmp::Read(const CString& strType, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	IW::MetaData data;
	HRESULT hr = data.LoadFromStream(pStreamIn);

	if (FAILED(hr))
		return false;

	DWORD nSize = data.GetDataSize();
	LPBYTE pByte = data.GetData();

	if (pByte == nullptr || nSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
	{
		return false;
	}

	// Dib Header
	auto pInfo = (BITMAPINFO*)(pByte + sizeof(BITMAPFILEHEADER));

	// Check that we got a real Windows DIB 
	if (pInfo->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
	{
		return FALSE;
	}

	// I only want single plain headers 
	if (pInfo->bmiHeader.biPlanes != 1)
	{
		return FALSE;
	}

	IW::PixelFormat pf = IW::PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);
	UINT nWidth = pInfo->bmiHeader.biWidth;
	UINT nHeight = abs(pInfo->bmiHeader.biHeight); // negative biHeight means a top-down DIB

	auto pRgb = (LPRGBQUAD)((LPSTR)pInfo + static_cast<WORD>(pInfo->bmiHeader.biSize));
	UINT nColors = pInfo->bmiHeader.biClrUsed;
	const UINT nMaxColors = pf.NumberOfPaletteEntries();

	if (nColors == 0 || nColors > nMaxColors)
	{
		nColors = nMaxColors;
	}

	if (nWidth == 0 || nHeight == 0)
	{
		return false;
	}

	// We really need to calculate the bfOffBits
	// ourselves as if it is corrupt a GPF may occur!
	auto pBitmap = (LPBYTE)(pRgb + nColors);

	const ptrdiff_t nBytesAvailable = static_cast<ptrdiff_t>(nSize) - (pBitmap - pByte);

	if (nBytesAvailable <= 0)
	{
		return false;
	}

	int nBytesMax = static_cast<int>(nBytesAvailable > INT_MAX ? INT_MAX : nBytesAvailable);

	IW::Image image;

	if (!image.Copy(pInfo, pBitmap, nBytesMax))
	{
		return false;
	}

	IW::IterateImage(image, *pImageOut, pStatus);

	// Information
	CString strInformation;
	strInformation.Format(IDS_BMP_FMT, nWidth, nHeight, pf.ToBpp());

	IW::CameraSettings settings;
	settings.XPelsPerMeter = pInfo->bmiHeader.biXPelsPerMeter;
	settings.YPelsPerMeter = pInfo->bmiHeader.biYPelsPerMeter;
	settings.OriginalImageSize.cx = nWidth;
	settings.OriginalImageSize.cy = nHeight;
	settings.OriginalBpp = pf;

	pImageOut->SetCameraSettings(settings);
	pImageOut->SetLoaderName(GetKey());
	pImageOut->SetStatistics(strInformation);

	return true;
}

bool CLoadBmp::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	BITMAPFILEHEADER hdr;

	IW::Page page = imageIn.GetFirstPage();
	IW::PixelFormat pf = page.GetPixelFormat();

	int cx = page.GetWidth();
	int cy = page.GetHeight();
	int nPaletteEntries = pf.NumberOfPaletteEntries();
	int nStorageWidth = IW::CalcStorageWidth(cx, pf);
	int nSizeImage = nStorageWidth * cy;
	int nSizeHeader = sizeof(BITMAPINFOHEADER) + nPaletteEntries * sizeof(RGBQUAD);


	// Fill in the fields of the file header
	hdr.bfType = 0x4d42;
	hdr.bfSize = nSizeImage + nSizeHeader + sizeof(BITMAPFILEHEADER);
	hdr.bfReserved1 = 0;
	hdr.bfReserved2 = 0;
	hdr.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + nSizeHeader);

	pStreamOut->Write(&hdr, sizeof(BITMAPFILEHEADER), nullptr);

	// Fill in the header info.
	BITMAPINFOHEADER bmh;
	bmh.biSize = sizeof(BITMAPINFOHEADER);
	bmh.biWidth = cx;
	bmh.biHeight = cy;
	bmh.biPlanes = 1;
	bmh.biBitCount = static_cast<WORD>(pf.ToBpp());
	bmh.biCompression = BI_RGB;
	bmh.biSizeImage = nSizeImage;
	bmh.biXPelsPerMeter = imageIn.GetXPelsPerMeter();
	bmh.biYPelsPerMeter = imageIn.GetYPelsPerMeter();
	bmh.biClrUsed = nPaletteEntries;
	bmh.biClrImportant = 0;

	// this struct already DWORD aligned!
	// Write the DIB header and the bits 

	pStreamOut->Write(&bmh, sizeof(bmh), nullptr);
	pStreamOut->Write(page.GetPalette(), nPaletteEntries * sizeof(RGBQUAD), nullptr);

	if (pf == IW::PixelFormat::PF32Alpha || pf == IW::PixelFormat::PF32)
	{
		IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

		IW::CBuffer<COLORREF> pLine(cx + 1);

		// The header declares 32 bits, so the alpha byte is part of the file.
		// RenderLine composites it away and returns zero, which writes a fully
		// transparent image; GetLine is the read that keeps it.
		const bool bAlpha = pf == IW::PixelFormat::PF32Alpha;

		for (int y = 0; y < cy; ++y)
		{
			if (bAlpha)
				pLock->GetLine(pLine, cy - (y + 1), 0, cx);
			else
				pLock->RenderLine(pLine, cy - (y + 1), 0, cx);

			pStreamOut->Write(pLine, nStorageWidth, nullptr);
		}
	}
	else
	{
		for (int y = 0; y < cy; ++y)
		{
			pStreamOut->Write(page.GetBitmapLine(cy - (y + 1)), nStorageWidth, nullptr);
		}
	}

	return TRUE;
}
