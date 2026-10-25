// ImageWalker by Zac Walker
//
// Purpose: Windows BMP and DIB implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"

#include "FileBmp.h"
#include "LibfilesDocument.h"
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
	BITMAPINFOHEADER header{};
	DWORD header_bytes{};
	pStreamIn->Seek(IW::IStreamCommon::eBegin, sizeof(BITMAPFILEHEADER));
	pStreamIn->Read(&header, sizeof(header), &header_bytes);
	pStreamIn->Seek(IW::IStreamCommon::eBegin, 0);
	if (header_bytes == sizeof(header) && header.biCompression == BI_RGB)
		return iw::imaging::detail::read_libfiles_document(libfiles::image_format::bmp,
			GetKey(), pStreamIn, pImageOut, pStatus);

	// Compressed DIBs retain the Windows SDK rendering path.
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
	const auto* fileHeader = reinterpret_cast<const BITMAPFILEHEADER*>(pByte);
	if (fileHeader->bfType != 0x4d42 || pInfo->bmiHeader.biWidth <= 0 ||
		pInfo->bmiHeader.biHeight == 0 || pInfo->bmiHeader.biHeight == INT_MIN)
	{
		pStatus->SetError(L"Invalid BMP header or dimensions.");
		return false;
	}

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

	const auto minimumOffset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
		static_cast<std::uint64_t>(nColors) * sizeof(RGBQUAD) +
		(pInfo->bmiHeader.biCompression == BI_BITFIELDS ? 3 * sizeof(DWORD) : 0);
	// bfSize is informational and often wrong, and some writers leave bfOffBits zero
	// to mean "after the palette". A nonzero offset must still lie inside the file.
	const auto offset = fileHeader->bfOffBits != 0 ? fileHeader->bfOffBits : static_cast<DWORD>(minimumOffset);
	const auto stride = ((static_cast<std::uint64_t>(nWidth) * pf.ToStorageBpp() + 31) / 32) * 4;
	const auto bytes = stride * nHeight;
	if (offset < minimumOffset || offset >= nSize ||
		((pInfo->bmiHeader.biCompression == BI_RGB || pInfo->bmiHeader.biCompression == BI_BITFIELDS) &&
			bytes > nSize - offset))
	{
		pStatus->SetError(L"The BMP pixel data is truncated or its offset is invalid.");
		return false;
	}

	auto* pBitmap = pByte + offset;
	const int nBytesMax = static_cast<int>((std::min)(nSize - offset, static_cast<DWORD>(INT_MAX)));

	IW::Image image;

	if (!image.Copy(pInfo, pBitmap, nBytesMax))
	{
		return false;
	}
	if (pf.HasPalette())
	{
		auto palette = image.GetFirstPage().GetPalette();
		for (UINT i = 0; i < nMaxColors; ++i) palette[i] |= 0xff000000;
	}

	IW::IterateImage(image, *pImageOut, pStatus);

	// Information
	CString strInformation;
	strInformation.Format(IW::Text::BMP_FMT, nWidth, nHeight, pf.ToBpp());

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
	str.Format(IW::Text::ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
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
