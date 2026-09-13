// ImageWalker by Zac Walker
//
// Purpose: PCX implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

#include "FilePcx.h"
#include "ImagingStreams.h"

#include <pshpack1.h>

using PcxHeader = struct
{
	BYTE manufacturer;   // always 0x0a
	BYTE version;
	BYTE encoding;       // always 1 (run length)
	BYTE bits_per_pixel; // bits per pixel per plane
	short xmin, ymin;
	short xmax, ymax;
	short hres;
	short vres;
	BYTE palette16[48];  // the 16 entry EGA palette
	BYTE reserved;
	BYTE color_planes;
	short bytes_per_line; // per plane, always even
	short palette_type;
	BYTE filler[58];
};

#include <poppack.h>

// The 256 entry VGA palette is appended to the file, behind this marker.
enum { PCX_VGA_PALETTE_MARKER = 0x0c, PCX_VGA_PALETTE_SIZE = 768 };

CLoadPcx::CLoadPcx() : _nRepCount(0), _nRepByte(0)
{
}

CLoadPcx::~CLoadPcx()
{
}

bool CLoadPcx::DecodeRow(LPCBYTE& p, LPCBYTE pEnd, LPBYTE pOut, int nCount)
{
	for (int n = 0; n < nCount; n++)
	{
		if (_nRepCount == 0)
		{
			if (p >= pEnd)
				return false;

			const BYTE b = *p++;

			if ((b & 0xc0) == 0xc0)
			{
				_nRepCount = b & 0x3f;

				// A zero length run would leave this loop making no progress.
				if (_nRepCount == 0 || p >= pEnd)
					return false;

				_nRepByte = *p++;
			}
			else
			{
				_nRepCount = 1;
				_nRepByte = b;
			}
		}

		pOut[n] = _nRepByte;
		_nRepCount--;
	}

	return true;
}

bool CLoadPcx::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	IW::MetaData data;

	if (FAILED(data.LoadFromStream(pStreamIn)))
		return false;

	const DWORD nSize = data.GetDataSize();
	LPCBYTE pData = data.GetData();

	if (pData == nullptr || nSize < sizeof(PcxHeader))
		return false;

	const auto pHeader = reinterpret_cast<const PcxHeader*>(pData);

	if (pHeader->manufacturer != 0x0a || pHeader->encoding != 1)
		return false;

	const int cx = 1 + pHeader->xmax - pHeader->xmin;
	const int cy = 1 + pHeader->ymax - pHeader->ymin;
	const int nBits = pHeader->bits_per_pixel;
	const int nPlanes = pHeader->color_planes;
	const int nBytesPerLine = pHeader->bytes_per_line;

	if (cx <= 0 || cy <= 0 || nBytesPerLine <= 0)
		return false;

	const bool bTrueColor = nPlanes == 3 && nBits == 8;
	const bool bIndexed = nPlanes == 1 && (nBits == 1 || nBits == 2 || nBits == 4 || nBits == 8);

	if (!bTrueColor && !bIndexed)
		return false;

	// bytes_per_line is what the run length decoder is driven by, so it has to
	// cover the row it claims to describe.
	if (nBytesPerLine < (cx * nBits + 7) / 8)
		return false;

	const IW::PixelFormat pf = bTrueColor ? IW::PixelFormat(IW::PixelFormat::PF24) : IW::PixelFormat::FromBpp(nBits);
	const int nStorageWidth = IW::CalcStorageWidth(cx, pf);
	const int nRowBytes = nBytesPerLine * nPlanes;

	pImageOut->CreatePage(CRect(0, 0, cx, cy), pf, true);

	LPCBYTE p = pData + sizeof(PcxHeader);
	LPCBYTE pEnd = pData + nSize;

	if (bIndexed)
	{
		COLORREF palette[256] = {};
		const int nEntries = pf.NumberOfPaletteEntries();
		LPCBYTE pRgb = pHeader->palette16;

		// palette16 is 48 bytes -- sixteen entries. Only the trailing VGA block
		// can feed a 256 entry palette.
		int nRead = IW::Min(nEntries, static_cast<int>(sizeof(pHeader->palette16)) / 3);

		// 256 colour files carry the palette after the pixels; keep the decoder
		// out of it as well as reading it.
		if (nBits == 8 &&
			nSize > PCX_VGA_PALETTE_SIZE &&
			pData[nSize - PCX_VGA_PALETTE_SIZE - 1] == PCX_VGA_PALETTE_MARKER)
		{
			pRgb = pData + nSize - PCX_VGA_PALETTE_SIZE;
			pEnd = pRgb - 1;
			nRead = IW::Min(nEntries, PCX_VGA_PALETTE_SIZE / 3);
		}

		for (int i = 0; i < nRead; i++)
		{
			// The low byte of an IW COLORREF is blue -- see docs/features.md.
			palette[i] = RGB(pRgb[2], pRgb[1], pRgb[0]) | 0xff000000;
			pRgb += 3;
		}

		// Entries the file did not describe are still read as alpha downstream.
		for (int i = nRead; i < nEntries; i++)
		{
			palette[i] = 0xff000000;
		}

		// A monochrome file with no palette of its own is black on white.
		if (nBits == 1 && (palette[0] & 0x00ffffff) == (palette[1] & 0x00ffffff))
		{
			palette[0] = 0xff000000;
			palette[1] = 0xffffffff;
		}

		pImageOut->SetPalette(palette);
	}

	IW::CBuffer<BYTE> planes(nRowBytes);
	IW::CBuffer<BYTE> line(IW::Max(nStorageWidth, nRowBytes), 0);

	int y = 0;

	for (; y < cy; y++)
	{
		if (!DecodeRow(p, pEnd, planes, nRowBytes))
			break;

		if (bTrueColor)
		{
			LPCBYTE pR = planes;
			LPCBYTE pG = pR + nBytesPerLine;
			LPCBYTE pB = pG + nBytesPerLine;
			LPBYTE pOut = line;

			for (int x = 0; x < cx; x++)
			{
				*pOut++ = pB[x];
				*pOut++ = pG[x];
				*pOut++ = pR[x];
			}
		}
		else
		{
			IW::MemCopy(line, planes, IW::Min(nBytesPerLine, nStorageWidth));
		}

		pImageOut->SetBitmap(y, line);

		if (pStatus != nullptr)
		{
			if (pStatus->QueryCancel())
				break;

			pStatus->Progress(y, cy);
		}
	}

	pImageOut->Flush();

	CString strInformation;
	strInformation.Format(_T("%dx%dx%d PCX"), cx, cy, nBits * nPlanes);

	IW::CameraSettings settings;
	settings.OriginalImageSize.cx = cx;
	settings.OriginalImageSize.cy = cy;
	settings.OriginalBpp = pf;

	pImageOut->SetCameraSettings(settings);
	pImageOut->SetLoaderName(GetKey());
	pImageOut->SetStatistics(strInformation);

	return y > 0;
}

bool CLoadPcx::Write(const CString& str, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	return false;
}
