// ImageWalker by Zac Walker
//
// Purpose: IW::Image implementation - page creation, cloning, histograms,
//          DIB and clipboard conversion, orientation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// Image : implementation file
//
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ViewModelItems.h"
#include "ImagingStreams.h"

#include "FileFormatAny.h"
#include "FileJpeg.h"

using namespace IW;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Image IW::CreatePreview(const Image& imageIn, CSize sizeIn, bool bHighQuality)
{
	constexpr int nDiv = 0x8000;
	CSize sizeAll = imageIn.GetBoundingRect().Size();

	int sx = MulDiv(sizeIn.cx, nDiv, sizeAll.cx);
	int sy = MulDiv(sizeIn.cy, nDiv, sizeAll.cy);

	if (sy < sx)
	{
		sizeIn.cx = MulDiv(sizeIn.cy, sizeAll.cx, sizeAll.cy);
	}
	else
	{
		sizeIn.cy = MulDiv(sizeIn.cx, sizeAll.cy, sizeAll.cx);
	}

	Image imageOut;

	// The stream writes its last band from its destructor, so it has to be gone
	// before the image is returned.
	if (bHighQuality)
	{
		ImageStreamThumbnail<CNull, true> thumbnailStream(imageOut, Search::Any, sizeIn);
		IterateImage(imageIn, thumbnailStream, CNullStatus::Instance);
	}
	else
	{
		ImageStreamThumbnail<CNull> thumbnailStream(imageOut, Search::Any, sizeIn);
		IterateImage(imageIn, thumbnailStream, CNullStatus::Instance);
	}

	return imageOut;
}


void Image::GetHistogram(Histogram& histogram) const
{
	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IW::CBuffer<COLORREF> pBuffer(nWidth);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);

			for (int x = 0; x < nWidth; x++)
			{
				histogram.AddColor(pBuffer[x]);
			}
		}
	}

	histogram.CalcMax();
}


void Image::FixOrientation(IStatus* pStatus)
{
	if (App.Settings._bExifAutoRotate && _settings.Orientation != Orientation::TopLeft)
	{
		Image imageTemp;

		if (HasMetaData(MetaDataTypes::JPEG_IMAGE))
		{
			JXFORM_CODE code = JXFORM_NONE;

			switch (_settings.Orientation)
			{
			case Orientation::LeftBottom:
				code = JXFORM_ROT_270;
				break;

			case Orientation::RightTop:
				code = JXFORM_ROT_90;
				break;

			default:
				break;
			}

			ImageStream<IImageStream> imageStream(imageTemp);
			CJpegTransformation trans(code, &imageStream, *this, pStatus);
			IterateMetaData(&trans);
		}
		else
		{
			switch (_settings.Orientation)
			{
			case Orientation::LeftBottom:
				Rotate270(*this, imageTemp, pStatus);
				break;

			case Orientation::RightTop:
				Rotate90(*this, imageTemp, pStatus);
				break;

			default:
				break;
			}
		}

		if (!imageTemp.IsEmpty())
		{
			imageTemp._settings.Orientation = Orientation::TopLeft;
			Copy(imageTemp);
		}
	}
};


bool Image::Copy(const BITMAPINFOHEADER* pBmpFileHdr, int nSize)
{
	// Read the file header to get the file size and to
	// find out where the bits start in the 
	auto pByte = (LPBYTE)pBmpFileHdr;


	// Check that we got a real Windows DIB 
	if (pBmpFileHdr->biSize != sizeof(BITMAPINFOHEADER))
	{
		return FALSE;
	}

	if (nSize == -1)
		nSize = pBmpFileHdr->biSizeImage;

	// I only want single plain headers 
	if (pBmpFileHdr->biPlanes != 1)
	{
		return FALSE;
	}

	PixelFormat pf = PixelFormat::FromBpp(pBmpFileHdr->biBitCount);
	UINT nHeight = pBmpFileHdr->biHeight;

	auto pRgb = (LPRGBQUAD)(pByte + static_cast<WORD>(pBmpFileHdr->biSize));
	UINT nColors = pBmpFileHdr->biClrUsed;
	const UINT nMaxColors = pf.NumberOfPaletteEntries();

	if (nColors == 0 || nColors > nMaxColors)
	{
		nColors = nMaxColors;
	}

	assert(nHeight > 0);

	// We really need to calculate the bfOffBits
	// ourselves as if it is corrupt a GPF may occur!
	auto pBytes = (LPCBYTE)(pRgb + nColors);

	// Do this in 64-bit: truncating to int lets a huge biClrUsed come back positive.
	const ptrdiff_t nOffset = pBytes - pByte;

	if (nOffset < 0 || nOffset > nSize)
	{
		return false;
	}

	const int nBytesMax = static_cast<int>(nSize - nOffset);

	return Copy((const BITMAPINFO*)pBmpFileHdr, pBytes, nBytesMax);
}


bool Image::Copy(const BITMAPINFO* pInfo, LPCBYTE pBytes, int nBytesMax)
{
	bool bSuccess = false;


	// Check that we got a real Windows image 
	if (pInfo->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
		return false;

	const int nWidth = pInfo->bmiHeader.biWidth;
	const int nHeight = abs(pInfo->bmiHeader.biHeight);


	// If its a complicated image use
	// SetDIBits
	if (pInfo->bmiHeader.biPlanes != 1 ||
		pInfo->bmiHeader.biCompression != BI_RGB)
	{
		const CRect rcCreate(0, 0, nWidth, nHeight);

		CRender render;

		if (render.Create(nullptr, rcCreate))
		{
			CDCRender dc(render);

			StretchDIBits(dc,
			              0, 0,
			              nWidth, nHeight,
			              0, 0,
			              nWidth, nHeight,
			              pBytes,
			              pInfo, // BITMAPINFO
			              DIB_RGB_COLORS, // Options
			              SRCCOPY); // Raster operation code (ROP)


			render.RenderToSurface(*this);
			bSuccess = true;
		}
	}
	else
	{
		const PixelFormat pf = PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);

		Page page = CreatePage(nWidth, nHeight, pf);
		const int nStorageWidth = CalcStorageWidth(nWidth, pf);
		int nColors = pInfo->bmiHeader.biClrUsed;
		const int nMaxColors = pf.NumberOfPaletteEntries();

		if (nColors <= 0 || nColors > nMaxColors)
		{
			nColors = nMaxColors;
		}

		auto pRgb = (LPCCOLORREF)((LPSTR)pInfo + static_cast<WORD>(pInfo->bmiHeader.biSize));

		if (nColors && pf.HasPalette())
			MemCopy(page.GetPalette(), pRgb, nColors * sizeof(COLORREF));

		const int nTotalBytes = nStorageWidth * nHeight;

		if (nBytesMax == -1)
		{
			nBytesMax = nTotalBytes;
		}
		else
		{
			nBytesMax = Min(nBytesMax, nTotalBytes);
		}

		// A short or corrupt source would otherwise leave the tail as raw heap.
		if (nBytesMax < nTotalBytes)
			MemZero(page.GetBitmap(), nTotalBytes);

		if (nBytesMax > 0)
		{
			if (pInfo->bmiHeader.biHeight < 0)
			{
				// Top-down source into bottom-up storage: copy row by row.
				const int nRows = Min(nHeight, nBytesMax / nStorageWidth);

				for (int y = 0; y < nRows; ++y)
					MemCopy(page.GetBitmapLine(y), pBytes + (nStorageWidth * y), nStorageWidth);
			}
			else
			{
				MemCopy(page.GetBitmap(), pBytes, nBytesMax);
			}
		}

		bSuccess = true;
	}


	SetXPelsPerMeter(pInfo->bmiHeader.biXPelsPerMeter);
	SetYPelsPerMeter(pInfo->bmiHeader.biYPelsPerMeter);


	return bSuccess;
}


bool Image::Copy(HDC hDC, HBITMAP hBitmap)
{
	assert(hBitmap != NULL);

	BITMAP bm;
	if (!::GetObject(hBitmap, sizeof(BITMAP), &bm))
		return false;

	const int nWidth = bm.bmWidth;
	const int nHeight = bm.bmHeight;
	bool bSuccess = false;
	const CRect rcCreate(0, 0, nWidth, nHeight);

	CRender render;

	if (render.Create(nullptr, rcCreate))
	{
		CDC dcIn;
		if (dcIn.CreateCompatibleDC(nullptr))
		{
			HBITMAP hbmOld = dcIn.SelectBitmap(hBitmap);

			if (hbmOld)
			{
				CDCRender dcOut(render);

				StretchBlt(dcOut,
				           0, 0,
				           nWidth, nHeight,
				           dcIn,
				           0, 0,
				           nWidth, nHeight,
				           SRCCOPY);

				dcIn.SelectBitmap(hbmOld);

				render.RenderToSurface(*this);
				bSuccess = true;
			}
		}
	}

	return bSuccess;
}


bool Image::Copy(HGLOBAL hglb)
{
	bool bSuccess = false;

	const SIZE_T cb = ::GlobalSize(hglb);
	auto pBytes = static_cast<LPBYTE>(GlobalLock(hglb));

	if (pBytes != nullptr)
	{
		// Image Header
		auto pInfo = (BITMAPINFO*)pBytes;

		if (cb >= sizeof(BITMAPINFOHEADER) &&
			pInfo->bmiHeader.biSize >= sizeof(BITMAPINFOHEADER) &&
			pInfo->bmiHeader.biSize <= cb)
		{
			// Check that we got a real Windows image 
			// I only want single plain headers 
			const PixelFormat pf = PixelFormat::FromBpp(pInfo->bmiHeader.biBitCount);
			SIZE_T nColors = pInfo->bmiHeader.biClrUsed;
			const SIZE_T nMaxColors = pf.NumberOfPaletteEntries();

			if (nColors == 0 || nColors > nMaxColors)
			{
				nColors = nMaxColors;
			}

			if (pInfo->bmiHeader.biCompression == BI_BITFIELDS)
			{
				nColors = 3;
			}

			const SIZE_T nOffset = pInfo->bmiHeader.biSize + (nColors * 4);

			if (nOffset < cb)
			{
				auto pBitmap = pBytes + nOffset;
				const SIZE_T nBytesMax = cb - nOffset;

				bSuccess = Copy(pInfo, pBitmap, static_cast<int>(nBytesMax > INT_MAX ? INT_MAX : nBytesMax));
			}
		}

		GlobalUnlock(hglb);
	}

	return bSuccess;
}


HGLOBAL Image::CopyToHandle() const
{
	HANDLE hCopy;

	if (IsEmpty())
		return nullptr;

	Page page = GetFirstPage();

	// How large is this image
	PixelFormat pf = page.GetPixelFormat();
	int nPaletteEntries = pf.NumberOfPaletteEntries();
	int cx = page.GetWidth();
	int cy = page.GetHeight();
	int nStorageWidth = CalcStorageWidth(cx, pf);
	int nSizeImage = nStorageWidth * cy;
	// The header written below is a BITMAPINFOHEADER, not our IWBITMAPINFO.
	int nSizeHeader = sizeof(BITMAPINFOHEADER) + nPaletteEntries * sizeof(RGBQUAD);


	// Alloc memory
	hCopy = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, nSizeImage + nSizeHeader);

	if (hCopy == nullptr)
		throw std::bad_alloc();

	auto lpCopy = static_cast<LPBYTE>(GlobalLock(hCopy));

	if (lpCopy == nullptr)
		throw std::bad_alloc();

	CImageBitmapInfoHeader bmi(GetFirstPage());
	MemCopy(lpCopy, bmi, sizeof(BITMAPINFOHEADER));

	if (nPaletteEntries != 0)
	{
		MemCopy(lpCopy + sizeof(BITMAPINFOHEADER), page.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
	}

	MemCopy(lpCopy + nSizeHeader, page.GetBitmap(), nSizeImage);

	GlobalUnlock(hCopy);

	return hCopy;
}

void Image::AddImageData(IStreamIn* pStreamIn, DWORD dwType)
{
	pStreamIn->Seek(IStreamCommon::eBegin, 0);

	MetaData data(dwType, pStreamIn);

	for (auto i = Blobs.begin(); i != Blobs.end(); ++i)
	{
		if (i->IsType(dwType))
		{
			*i = data;
			return;
		}
	}

	SetMetaData(data);
}

Image Image::LoadPreviewImage(ImageLoaders& loaders)
{
	Image image;
	CLoadAny loader(loaders);
	StreamResource f(App.GetResourceInstance(), IDR_PREVIEW_SAMPLE);
	loader.Read(_T("JPG"), &f, image, CNullStatus::Instance);
	return image;
}


bool Image::Render(Image& imageOut) const
{
	const CRect rc = GetBoundingRect();

	CRender render;
	render.Create(nullptr, rc);
	render.Fill(RGB(0, 0, 0));

	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		Page pageOut = imageOut.CreatePage(rc, PixelFormat::PF32);

		if (pageIn->GetDisposalMethod() == 2)
		{
			render.Fill(RGB(0, 0, 0));
		}

		// Render to surface then copy to destination
		render.DrawImage(*pageIn, 0, 0);
		render.RenderToSurface(pageOut, 0, 0);

		if (pageIn->GetFlags() & PageFlags::HasTimeDelay)
		{
			pageOut.SetTimeDelay(pageIn->GetTimeDelay());
		}
	}

	return true;
}


bool IW::Crop(const Image& imageIn, Image& imageOut, const CRect& rectCrop, IStatus* pStatus)
{
	const CRect rcBounding = imageIn.GetBoundingRect();

	IW::CBuffer<COLORREF> pLine(rcBounding.right - rcBounding.left);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rcOrg = pageIn->GetPageRect();

		CRect rc;
		if (IntersectRect(&rc, &rectCrop, &rcOrg))
		{
			PixelFormat pf = pageIn->GetPixelFormat();
			Page pageOut = imageOut.CreatePage(rc, pf);

			const int nPaletteEntries = pf.NumberOfPaletteEntries();

			if (nPaletteEntries != 0)
			{
				MemCopy(pageOut.GetPalette(), pageIn->GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
			}

			const int nWidth = rc.right - rc.left;
			const int nHeight = rc.bottom - rc.top;
			const int nStartX = rc.left - rcOrg.left;
			const int nStartY = rc.top - rcOrg.top;

			IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();
			ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

			for (int y = 0; y < nHeight; y++)
			{
				pLockIn->GetLine(pLine, y + nStartY, nStartX, nWidth);
				pLockOut->SetLine(pLine, y, 0, nWidth);
			}

			pageOut.CopyExtraInfo(*pageIn);
		}
	}

	imageOut.Normalize();

	return true;
}


bool IW::MirrorTB(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		Page pageOut = imageOut.CreatePage(*pageIn);

		const int nHeight = pageIn->GetHeight();
		const int nStorageWidth = pageIn->GetStorageWidth();

		for (int y = 0; y < nHeight; y++)
		{
			int y1 = nHeight - (y + 1);
			MemCopy(pageOut.GetBitmapLine(y1), pageIn->GetBitmapLine(y), nStorageWidth);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


bool IW::MirrorLR(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		Page pageOut = imageOut.CreatePage(*pageIn);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		// GetLine and SetLine agree on what a value means for every format --
		// indices for the palettised ones, 888 for the rest -- so the mirror is a
		// reverse of the row rather than a pixel-at-a-time swap.
		IW::CBuffer<COLORREF> lineBuffer(nWidth);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->GetLine(lineBuffer, y, 0, nWidth);
			std::reverse(lineBuffer.data(), lineBuffer.data() + nWidth);
			pLockOut->SetLine(lineBuffer, y, 0, nWidth);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


bool IW::GrayScale(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		Page pageOut = imageOut.CreatePage(rc, PixelFormat::PF8GrayScale);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		int y, x;
		unsigned c;

		IW::CBuffer<COLORREF> pBuffer(nWidth);

		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

		for (y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);
			LPBYTE pLineOut = pageOut.GetBitmapLine(y);

			for (x = 0; x < nWidth; x++)
			{
				c = pBuffer[x];
				pLineOut[x] = (GetR(c) + GetG(c) + GetB(c)) / 3;
			}
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


bool IW::Dither(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		Page pageOut = imageOut.CreatePage(rc, PixelFormat::PF1);

		const int nHeight = pageIn->GetHeight();
		const int nWidth = pageIn->GetWidth();

		int y, x;
		unsigned c;

		IW::CBuffer<COLORREF> pBuffer(nWidth);
		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		for (y = 0; y < nHeight; y++)
		{
			pLockIn->RenderLine(pBuffer, y, 0, nWidth);

			for (x = 0; x < nWidth; x++)
			{
				c = pBuffer[x];
				pBuffer[x] = (GetR(c) + GetG(c) + GetB(c)) / 3;
			}


			// Multi-Level Ordered-Dithering by Kenny Hoff (Oct. 12, 1995)
			constexpr int NumRows = 4;
			constexpr int NumCols = 4;
			constexpr int NumIntensityLevels = 2;
			constexpr int NumRowsLessOne = (NumRows - 1);
			constexpr int NumColsLessOne = (NumCols - 1);
			constexpr int RowsXCols = (NumRows * NumCols);
			constexpr int MaxIntensityVal = 255;
			constexpr int MaxDitherIntensityVal = (NumRows * NumCols * (NumIntensityLevels - 1));

			int DitherMatrix[NumRows][NumCols] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

			unsigned char Intensity[NumIntensityLevels] = {1, 0}; // 2 LEVELS B/W
			//unsigned char Intensity[NumIntensityLevels] = { 0,255 };                       // 2 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,127,255 };                   // 3 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,85,170,255 };                // 4 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,63,127,191,255 };            // 5 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,51,102,153,204,255 };        // 6 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,42,85,127,170,213,255 };     // 7 LEVELS
			//unsigned char Intensity[NumIntensityLevels] = { 0,36,73,109,145,182,219,255 }; // 8 LEVELS
			int DitherIntensity, DitherMatrixIntensity, Offset, DeviceIntensity;

			for (x = 0; x < nWidth; x++)
			{
				DeviceIntensity = pBuffer[x];
				DitherIntensity = DeviceIntensity * MaxDitherIntensityVal / MaxIntensityVal;
				DitherMatrixIntensity = DitherIntensity % RowsXCols;
				Offset = DitherIntensity / RowsXCols;

				if (DitherMatrix[y & NumRowsLessOne][x & NumColsLessOne] < DitherMatrixIntensity)
				{
					pBuffer[x] = Intensity[1 + Offset];
				}
				else
				{
					pBuffer[x] = Intensity[0 + Offset];
				}
			}

			pLockOut->SetLine(pBuffer, y, 0, nWidth);
		}


		LPCOLORREF pRGBOut = pageOut.GetPalette();
		pRGBOut[0] = RGB(255, 255, 255);
		pRGBOut[1] = RGB(0, 0, 0);

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}

static bool Filter(const Page& pageIn, Image& imageOut, long* kernel, long Ksize, long Kfactor, long Koffset,
                   IStatus* pStatus)
{
	long k2 = Ksize / 2;
	long kmax = Ksize - k2;
	long r, g, b, a, i;
	COLORREF c;
	PixelFormat pf = pageIn.GetPixelFormat();
	int nWidth = pageIn.GetWidth();
	int nHeight = pageIn.GetHeight();
	int x, y, j, k;

	CRect rcOrg = pageIn.GetPageRect();
	CRect rc = rcOrg;

	Page pageOut = imageOut.CreatePage(rc, PixelFormat::PF24);
	ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
	IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

	IW::CBuffer<COLORREF> pLine(nWidth);
	CSimpleArray<LPCOLORREF> lines;

	for (k = 0; k < Ksize; k++)
	{
		auto pBuffer = static_cast<LPCOLORREF>(Alloc(nWidth * sizeof(COLORREF)));
		lines.Add(pBuffer);
	}

	for (y = 0; y < k2; y++)
	{
		pLockIn->RenderLine(pLine, y, 0, nWidth);
		pLockOut->SetLine(pLine, y, 0, nWidth);
	}

	bool bCancel = false;

	for (y = k2; y < nHeight - k2 && !bCancel; y++)
	{
		pLockIn->RenderLine(pLine, y, 0, nWidth);

		// Load the lines for this scan
		for (k = 0; k < Ksize; k++)
		{
			pLockIn->RenderLine(lines[k], y + k - k2, 0, nWidth);
		}

		for (x = k2; x < nWidth - k2; x++)
		{
			r = b = g = a = 0;

			for (j = -k2; j < kmax; j++)
			{
				for (k = -k2; k < kmax; k++)
				{
					LPCOLORREF pBuffer = lines[k + k2];
					int n = x + j;
					c = pBuffer[n];

					i = kernel[(j + k2) + Ksize * (k + k2)];

					r += GetR(c) * i;
					g += GetG(c) * i;
					b += GetB(c) * i;
					a += GetA(c) * i;
				}
			}

			if (Kfactor <= 1)
			{
				c = SaturateRGBA(
					r + Koffset,
					g + Koffset,
					b + Koffset,
					a + Koffset);
			}
			else
			{
				c = SaturateRGBA(
					r / Kfactor + Koffset,
					g / Kfactor + Koffset,
					b / Kfactor + Koffset,
					a / Kfactor + Koffset);
			}

			pLine[x] = c;
		}

		pLockOut->SetLine(pLine, y, 0, nWidth);
		pStatus->Progress(y, nHeight);
		bCancel = pStatus->QueryCancel();
	}

	for (; y < nHeight; y++)
	{
		pLockIn->RenderLine(pLine, y, 0, nWidth);
		pLockOut->SetLine(pLine, y, 0, nWidth);
	}

	pageOut.CopyExtraInfo(pageIn);

	for (k = 0; k < lines.GetSize(); k++)
	{
		Free(lines[k]);
	}

	lines.RemoveAll();

	return !bCancel;
}

bool IW::Filter(const Image& imageIn, Image& imageOut, long* kernel, long Ksize, long Kfactor, long Koffset,
                IStatus* pStatus)
{
	assert(imageOut.GetPageCount() == 0);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		::Filter(*pageIn, imageOut, kernel, Ksize, Kfactor, Koffset, pStatus);
	}

	imageOut.Normalize();

	return true;
}


static void ClippedSetPixel(Page& pageOut, IImageSurfaceLock* pLockOut, const int x, const int y, const UINT c)
{
	if (x >= 0 && x < pageOut.GetWidth() && y >= 0 && y < pageOut.GetHeight())
	{
		pLockOut->SetPixel(x, y, c);
	}
}

static void Frame(Page& pageOut, IImageSurfaceLock* pLockOut, CRect& rcFrame, int nOffset, COLORREF crColor)
{
	int nWidth = rcFrame.right - rcFrame.left;
	int nHeight = rcFrame.bottom - rcFrame.top;

	// First draw the frame
	for (int x = nOffset; x < nWidth - nOffset; x++)
	{
		ClippedSetPixel(pageOut, pLockOut, rcFrame.left + x, rcFrame.top + nOffset, crColor);
		ClippedSetPixel(pageOut, pLockOut, rcFrame.left + x, rcFrame.top + nHeight - nOffset - 1, crColor);
	}

	for (int y = nOffset; y < nHeight - nOffset; y++)
	{
		ClippedSetPixel(pageOut, pLockOut, rcFrame.left + nOffset, rcFrame.top + y, crColor);
		ClippedSetPixel(pageOut, pLockOut, rcFrame.left + nWidth - nOffset - 1, rcFrame.top + y, crColor);
	}
}

bool IW::Frame(const Image& imageIn, Image& imageOut, COLORREF clrFrame, IStatus* pStatus)
{
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

		constexpr int nFrameWidth = 1;

		CRect rc = pageIn->GetPageRect();
		CRect rcCreate = rc;
		rc.OffsetRect(nFrameWidth, nFrameWidth);
		rcCreate.right += nFrameWidth * 2;
		rcCreate.bottom += nFrameWidth * 2;

		CRect rcFrame = rcCreate;
		Page pageOut = imageOut.CreatePage(rcCreate, PixelFormat::PF24);
		IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		int nWidthOut = rcCreate.right - rcCreate.left;
		int nWidthIn = rc.right - rc.left;
		int nHeightIn = rc.bottom - rc.top;

		// Allocate a line buffer
		IW::CBuffer<COLORREF> pLine(nWidthOut);

		// First draw the frame
		::Frame(pageOut, pLockOut, rcFrame, 0, SwapRB(clrFrame));

		int xx = rc.left - rcCreate.left;
		int yy = rc.top - rcCreate.top;

		for (int y = 0; y < nHeightIn; y++)
		{
			pLockIn->RenderLine(pLine, y, 0, nWidthIn);
			pLockOut->SetLine(pLine, y + yy, xx, nWidthIn);
			pStatus->Progress(y, nHeightIn);
			if (pStatus->QueryCancel()) return false;
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}


bool Page::CompareBits(const Page& other) const
{
	if (_pBlob->dwSize != other._pBlob->dwSize ||
		_pBlob->pf != other._pBlob->pf ||
		_pBlob->dwTimeDelay != other._pBlob->dwTimeDelay ||
		_pBlob->dwBackGround != other._pBlob->dwBackGround ||
		_pBlob->dwTransparent != other._pBlob->dwTransparent ||
		_pBlob->rectPage.left != other._pBlob->rectPage.left ||
		_pBlob->rectPage.top != other._pBlob->rectPage.top ||
		_pBlob->rectPage.right != other._pBlob->rectPage.right ||
		_pBlob->rectPage.bottom != other._pBlob->rectPage.bottom)
	{
		return false;
	}

	if (memcmp(GetData(), other.GetData(), GetDataSize()) != 0)
	{
		return false;
	}

	return true;
}

bool Page::CopyExtraInfo(const Page& other)
{
	SetTimeDelay(other.GetTimeDelay());
	SetTransparent(other.GetTransparent());
	SetBackGround(other.GetBackGround());
	SetFlags(other.GetFlags());

	return false;
}

CString Image::ToString() const
{
	CString str;
	str.Format(_T(
			   "PageCount = %Iu, MetaDataCount = %Iu, PelsPerMeter = %d*%d, Title=%s, Tags=%s, Description=%s, ObjectName=%s, Statistics = %s, LoaderName = %s, Flags = %d, OriginalImageSize = %d*%d, OriginalBpp = %d"),
			   Pages.size(),
			   Blobs.size(),
			   _settings.XPelsPerMeter,
			   _settings.YPelsPerMeter,
			   static_cast<LPCTSTR>(_strTitle),
			   static_cast<LPCTSTR>(_strTags),
			   static_cast<LPCTSTR>(_strDescription),
			   static_cast<LPCTSTR>(_strObjectName),
			   static_cast<LPCTSTR>(_strStatistics),
			   static_cast<LPCTSTR>(_strLoaderName),
			   _dwFlags,
	           _settings.OriginalImageSize.cx,
	           _settings.OriginalImageSize.cy,
	           static_cast<int>(_settings.OriginalBpp._pf));

	return str;
}

CImageBitmapInfoHeader::CImageBitmapInfoHeader(const Page& page)
{
	// GDI reads bmiColors for any 8-bpp header; leaving it as stack garbage leaks
	// uninitialised memory into the rendered image.
	MemZero(&m_info, sizeof(m_info));

	PixelFormat pf = page.GetPixelFormat();
	int nPaletteEntries = pf.NumberOfPaletteEntries();
	int cx = page.GetWidth();
	int cy = page.GetHeight();
	int nStorageWidth = CalcStorageWidth(cx, pf);
	int nSizeImage = nStorageWidth * cy;

	m_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	m_info.bmiHeader.biWidth = cx;
	m_info.bmiHeader.biHeight = cy;
	m_info.bmiHeader.biPlanes = 1;
	m_info.bmiHeader.biBitCount = static_cast<WORD>(pf.ToBpp());
	m_info.bmiHeader.biCompression = BI_RGB;
	m_info.bmiHeader.biSizeImage = nSizeImage;
	m_info.bmiHeader.biXPelsPerMeter = 0;
	m_info.bmiHeader.biYPelsPerMeter = 0;
	m_info.bmiHeader.biClrUsed = nPaletteEntries;
	m_info.bmiHeader.biClrImportant = 0;

	if (nPaletteEntries != 0)
	{
		MemCopy(m_info.bmiColors, page.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
	}
	else
	{
		// 16 or 32 bit need bitfields
		switch (pf._pf)
		{
		case PixelFormat::PF1:
		case PixelFormat::PF2:
		case PixelFormat::PF4:
		case PixelFormat::PF8:
		case PixelFormat::PF8Alpha:
		case PixelFormat::PF24:
			// Nothing todo
			break;
		case PixelFormat::PF8GrayScale:
			// No palette is stored with the page, so supply the grey ramp GDI needs.
			for (int i = 0; i < 256; ++i)
				m_info.bmiColors[i] = RGB(i, i, i);
			m_info.bmiHeader.biClrUsed = 256;
			break;
		case PixelFormat::PF555:
			m_info.bmiColors[0] = 0x00007c00;
			m_info.bmiColors[1] = 0x000003e0;
			m_info.bmiColors[2] = 0x0000001f;
			m_info.bmiHeader.biBitCount = 16;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
			break;
		case PixelFormat::PF565:
			m_info.bmiColors[0] = 0x0000f800;
			m_info.bmiColors[1] = 0x000007e0;
			m_info.bmiColors[2] = 0x0000001f;
			m_info.bmiHeader.biBitCount = 16;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
			break;
		case PixelFormat::PF32:
		case PixelFormat::PF32Alpha:
			m_info.bmiColors[0] = 0x00ff0000;
			m_info.bmiColors[1] = 0x0000ff00;
			m_info.bmiColors[2] = 0x000000ff;
			m_info.bmiHeader.biBitCount = 32;
			m_info.bmiHeader.biCompression = BI_BITFIELDS;
		}
	}
}

void Image::ConvertTo(PixelFormat pf)
{
	Image image;

	for (auto pageIn = Pages.begin(); pageIn != Pages.end(); ++pageIn)
	{
		CRect rc = pageIn->GetPageRect();
		Page pageOut = image.CreatePage(rc, pf);

		const int cx = pageOut.GetWidth();
		const int cy = pageOut.GetHeight();

		ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
		IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();
		IW::CBuffer<COLORREF> pLine(cx);

		for (int y = 0; y < cy; y++)
		{
			pLockIn->RenderLine(pLine, y, 0, cx);
			pLockOut->SetLine(pLine, y, 0, cx);
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	Copy(image);
}

void Image::Normalize()
{
	if (!Pages.empty())
	{
		CRect rc = GetBoundingRect();

		if (rc.top != 0 || rc.left != 0)
		{
			int cx = 0 - rc.left;
			int cy = 0 - rc.top;

			for (auto i = Pages.begin(); i != Pages.end(); ++i)
			{
				rc = i->GetPageRect();
				int x = Min(rc.left, rc.right) + cx;
				int y = Min(rc.top, rc.bottom) + cy;
				i->SetPageOffset(x, y);
			}
		}
	}
}

bool Image::Compare(const Image& image, bool bCompareMetaData) const
{
	if (_strTitle != image._strTitle ||
		_strTags != image._strTags ||
		_strDescription != image._strDescription ||
		_strObjectName != image._strObjectName ||
		_strStatistics != image._strStatistics ||
		_strLoaderName != image._strLoaderName ||
		_dwFlags != image._dwFlags ||
		_strErrors != image._strErrors ||
		_strWarnings != image._strWarnings ||
		_settings != image._settings ||
		GetPageCount() != image.GetPageCount() ||
		Blobs.size() != image.Blobs.size())
	{
		return false;
	}

	unsigned nCount = GetPageCount();
	for (unsigned i = 0; i < nCount; ++i)
	{
		const Page p1 = GetPage(i);
		const Page p2 = image.GetPage(i);

		if (!p1.CompareBits(p2))
		{
			return false;
		}
	}

	for (auto i = Blobs.begin(); i != Blobs.end(); ++i)
	{
		DWORD type = i->GetType();

		if (bCompareMetaData)
		{
			const MetaData otherBlob = image.GetMetaData(type);
			if (i->GetDataSize() != otherBlob.GetDataSize()) return false;
			if (!i->IsEmpty() && memcmp(i->GetData(), otherBlob.GetData(), i->GetDataSize()) != 0) return false;
		}
	}

	return true;
}


Image Image::Clone() const
{
	Image other;

	for (auto i = Pages.begin(); i != Pages.end(); ++i)
		other.Pages.push_back(i->Clone());

	for (auto i = Blobs.begin(); i != Blobs.end(); ++i)
		other.Blobs.push_back(i->Clone());

	other._strTitle = _strTitle;
	other._strTags = _strTags;
	other._strDescription = _strDescription;
	other._strObjectName = _strObjectName;
	other._strStatistics = _strStatistics;
	other._strLoaderName = _strLoaderName;
	other._dwFlags = _dwFlags;
	other._settings = _settings;
	other._strErrors = _strErrors;
	other._strWarnings = _strWarnings;

	return other;
}

void Image::Copy(const Image& other)
{
	Pages = other.Pages;
	Blobs = other.Blobs;

	_strTitle = other._strTitle;
	_strTags = other._strTags;
	_strDescription = other._strDescription;
	_strObjectName = other._strObjectName;
	_strStatistics = other._strStatistics;
	_strLoaderName = other._strLoaderName;
	_dwFlags = other._dwFlags;
	_settings = other._settings;
	_strErrors = other._strErrors;
	_strWarnings = other._strWarnings;
}


// Create a new image
Page& Image::CreatePage(int cx, int cy, const PixelFormat& pf)
{
	Free();
	return CreatePage(CRect(0, 0, cx, cy), pf);
};

// Create a new image
Page& Image::CreatePage(const Page& pageIn)
{
	CRect rc = pageIn.GetPageRect();

	PixelFormat pf = pageIn.GetPixelFormat();
	Page& pageOut = CreatePage(rc, pf);
	int nPaletteEntries = pf.NumberOfPaletteEntries();

	if (nPaletteEntries != 0)
	{
		MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
	}

	return pageOut;
};

Page& Image::CreatePage(const CRect& rc, const PixelFormat& pf)
{
	int cx = rc.right - rc.left;
	int cy = rc.bottom - rc.top;

	// cx/cy come straight from file headers -- an int multiply here wraps.
	const __int64 nSizeImage64 =
		((((static_cast<__int64>(cx) * pf.ToStorageBpp()) + 7) / 8) + 3) / 4 * 4 * cy;

	if (cx <= 0 || cy <= 0 || nSizeImage64 > kMaxPageBytes)
		throw std::bad_alloc();

	Pages.push_back(Page());
	Page& page = Pages.back();

	int nPaletteEntries = pf.NumberOfPaletteEntries();

	// Calc storage GetWidth;
	int nStorageWidth = CalcStorageWidth(cx, pf);

	// Allocate memory for the bits (DWORD aligned).
	const DWORD nSizeImage = nStorageWidth * cy;
	const DWORD nSizePalette = nPaletteEntries * sizeof(RGBQUAD);

	PIWBITMAPINFO pIWBM = page.Alloc(nSizePalette + nSizeImage);
	pIWBM->dwBackGround = 0xffffffff;
	pIWBM->rectPage = rc;
	pIWBM->pf = pf;

	// Blob::Alloc only zeroes the header. An alpha page is composited *into* --
	// the blitter reads the destination back and leaves a fully transparent
	// pixel untouched -- so it must not start as heap residue.
	if (pf.HasAlpha())
	{
		MemZero(page.GetBitmap(), nSizeImage);
	}

	if (nPaletteEntries)
	{
		// Create an arbitrary color table (gray scale).
		LPCOLORREF pRGB = page.GetPalette();

		if (nPaletteEntries == 2)
		{
			pRGB[1] = RGB(255, 255, 255);
			pRGB[0] = RGB(0, 0, 0);
		}
		else
		{
			for (int i = 0; i < nPaletteEntries; i++)
			{
				*pRGB = RGB(i, i, i);
				pRGB++;
			}
		}
	}

	return page;
}


PixelFormat PixelFormat::FromBpp(int nBpp)
{
	switch (nBpp)
	{
	case 1:
		return PixelFormat(PF1);
	case 2:
		return PixelFormat(PF2);
	case 4:
		return PixelFormat(PF4);
	case 8:
		return PixelFormat(PF8);
	case 15:
	case 16:
		return PixelFormat(PF555);
	case 24:
		return PixelFormat(PF24);
	case 32:
	default:
		return PixelFormat(PF32);
	}
}

LPCTSTR PixelFormat::ToString() const
{
	switch (_pf)
	{
	case PF1:
		return _T("1");
	case PF2:
		return _T("2");
	case PF4:
		return _T("4");
	case PF8:
	case PF8Alpha:
	case PF8GrayScale:
		return _T("8");
	case PF555:
		return _T("555");
	case PF565:
		return _T("565");
	case PF24:
		return _T("888");
	}

	return _T("8888");
}


CString CameraSettings::FormatAperture() const
{
	CString str;
	if (Aperture.denominator != 0)
	{
		float f = Aperture.ToFloat() / 2.0f;
		str.Format(_T("f/%.01f"), pow(2, f));
	}
	return str;
}

CString CameraSettings::FormatIsoSpeed() const
{
	CString str;
	if (IsoSpeed != 0)
		str = IToStr(IsoSpeed);
	return str;
}

CString CameraSettings::FormatWhiteBalance() const
{
	CString str;
	if (WhiteBalance != 0)
		str = IToStr(WhiteBalance);
	return str;
}

CString CameraSettings::FormatExposureTime() const
{
	CString str;
	if (ExposureTime.denominator != 0)
	{
		float t = ExposureTime.ToFloat();

		if (t < 1.0)
		{
			str.Format(_T("1/%d s"), static_cast<int>(ceil(1.0f / t)));
		}
		else
		{
			str.Format(_T("%d s"), static_cast<int>(t));
		}
	}
	return str;
}

CString CameraSettings::FormatFocalLength() const
{
	CString str;
	if (FocalLength.denominator != 0)
	{
		str.Format(_T("%.1f mm"), FocalLength.ToFloat());
	}

	if (FocalLength35mmEquivalent != 0)
	{
		CString str35mm;
		str35mm.Format(_T(" (%dmm film eq)"), FocalLength35mmEquivalent);
		str += str35mm;
	}

	return str;
}
