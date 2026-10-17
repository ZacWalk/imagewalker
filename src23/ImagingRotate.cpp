// ImageWalker by Zac Walker
//
// Purpose: Right-angle rotation and the three-shear rotation by an arbitrary
//          angle.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"
#include "ImagingLock.h"
#include "ImagingStreams.h"

namespace
{
	// Right-angle rotation, a tile of source rows at a time. The output is
	// written as SetLine runs rather than a call per pixel, and the tile keeps
	// the transpose inside the cache.
	template<bool TClockwise, class TLockOut>
	void TransposePage(const IW::IImageSurfaceLock* pLockIn, TLockOut& lockOut,
	                   const int nWidth, const int nHeight, IW::IStatus* pStatus)
	{
		// 64 rows, but capped by bytes as well: a panorama is wide enough that a
		// row count alone would turn this into a multi-megabyte scratch buffer.
		const int nRowBytes = IW::Max(1, static_cast<int>(nWidth * sizeof(COLORREF)));
		const int nTile = IW::Min(IW::Min(64, IW::Max(1, (1024 * 1024) / nRowBytes)), nHeight);

		IW::CBuffer<COLORREF> tile(static_cast<size_t>(nTile) * nWidth);
		IW::CBuffer<COLORREF> run(nTile);

		for (int y0 = 0; y0 < nHeight; y0 += nTile)
		{
			const int n = IW::Min(nTile, nHeight - y0);

			for (int j = 0; j < n; j++)
			{
				pLockIn->GetLine(tile.data() + (static_cast<size_t>(j) * nWidth), y0 + j, 0, nWidth);
			}

			for (int x = 0; x < nWidth; x++)
			{
				if constexpr (TClockwise)
				{
					// Destination column is nHeight-1-y, so the run walks the tile backwards.
					for (int j = 0; j < n; j++)
						run[j] = tile[(static_cast<size_t>(n - 1 - j) * nWidth) + x];

					lockOut.SetLine(run, x, nHeight - y0 - n, n);
				}
				else
				{
					for (int j = 0; j < n; j++)
						run[j] = tile[(static_cast<size_t>(j) * nWidth) + x];

					lockOut.SetLine(run, nWidth - 1 - x, y0, n);
				}
			}

			pStatus->Progress(y0 + n, nHeight);
		}
	}
}

bool IW::Rotate90(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.top, rcOrg.left, rcOrg.bottom, rcOrg.right);
		PixelFormat pf = pageIn.GetPixelFormat();
		Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHeight = pageIn.GetHeight();

		WithSurfaceLock(pageOut, [&](auto& lockOut)
		{
			TransposePage<true>(pLockIn, lockOut, nWidth, nHeight, pStatus);
		});

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}

bool IW::Rotate270(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.top, rcOrg.left, rcOrg.bottom, rcOrg.right);
		PixelFormat pf = pageIn.GetPixelFormat();
		Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHeight = pageIn.GetHeight();

		WithSurfaceLock(pageOut, [&](auto& lockOut)
		{
			TransposePage<false>(pLockIn, lockOut, nWidth, nHeight, pStatus);
		});

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}


bool IW::Rotate180(const Image& imageIn, Image& imageOut, IStatus* pStatus)
{
	for (auto it = imageIn.Pages.begin(); it != imageIn.Pages.end(); ++it)
	{
		const Page& pageIn = *it;
		if (pStatus->QueryCancel()) return false;

		CRect rcOrg = pageIn.GetPageRect();
		CRect rc(rcOrg.left, rcOrg.top, rcOrg.right, rcOrg.bottom);
		PixelFormat pf = pageIn.GetPixelFormat();
		Page pageOut = imageOut.CreatePage(rc, pf);
		int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries != 0)
		{
			MemCopy(pageOut.GetPalette(), pageIn.GetPalette(), nPaletteEntries * sizeof(RGBQUAD));
		}

		ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
		IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

		const int nWidth = pageIn.GetWidth();
		const int nHalfWidth = nWidth / 2;
		const int nHeight = pageIn.GetHeight();

		IW::CBuffer<COLORREF> lineBuffer(nWidth + 1);

		for (int y = 0; y < nHeight; y++)
		{
			pLockIn->GetLine(lineBuffer, y, 0, nWidth);

			for (int x = 0; x < nHalfWidth; x++)
			{
				Swap(lineBuffer[x], lineBuffer[nWidth - (x + 1)]);
			}

			pLockOut->SetLine(lineBuffer, nHeight - (y + 1), 0, nWidth);

			pStatus->Progress(y, nHeight);
		}

		pageOut.CopyExtraInfo(pageIn);
	}

	imageOut.Normalize();
	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}


static inline COLORREF MinusCOLORREF(const COLORREF a, COLORREF b)
{
	return IW::RGBA(
		IW::GetR(a) - IW::GetR(b),
		IW::GetG(a) - IW::GetG(b),
		IW::GetB(a) - IW::GetB(b),
		IW::GetA(a) - IW::GetA(b));
}


static inline int interp2(const unsigned a, const unsigned b, const unsigned w)
{
	return (b + (a - b) * w) >> 8;
}

static inline COLORREF interp(COLORREF a, COLORREF b, const unsigned w)
{
	return IW::RGBA(
		interp2(IW::GetR(a), IW::GetR(b), w),
		interp2(IW::GetG(a), IW::GetG(b), w),
		interp2(IW::GetB(a), IW::GetB(b), w),
		interp2(IW::GetA(a), IW::GetA(b), w));
}

void SkewLine(LPCOLORREF pLineOut, IW::LPCCOLORREF pLineIn, const int len, const int lenOut, const int iOffset,
              const int weight, const COLORREF clrBack)
{
	// Fill gap left of skew with background
	COLORREF pxlOldLeft = clrBack;
	int n = 0;

	for (n = 0; n < iOffset; n++)
		pLineOut[n] = clrBack;

	for (n = 0; n < len; n++)
	{
		const COLORREF pxlSrc = pLineIn[n];
		const COLORREF pxlLeft = interp(pxlSrc, clrBack, weight);
		const int nOut = n + iOffset;

		if ((nOut >= 0) && (nOut < lenOut))
		{
			pLineOut[nOut] = MinusCOLORREF(pxlSrc, MinusCOLORREF(pxlLeft, pxlOldLeft));
		}

		pxlOldLeft = pxlLeft;
	}

	n = len + iOffset;

	if (n < lenOut)
	{
		pLineOut[n] = pxlOldLeft;
		n++;
	}

	for (; n < lenOut; n++)
		pLineOut[n] = clrBack;
}

// The locks and the two line buffers belong to the caller: this used to build a
// pair of heap locks and a pair of buffers for every single scan line.
template<class TLockIn, class TLockOut>
static void HorizontalSkew(
	const TLockIn* pLockIn,
	TLockOut* pLockOut,
	COLORREF* pLineIn,
	COLORREF* pLineOut,
	const int cx, // Source width
	const int cxOut, // Destination width
	unsigned y, // Row index
	int iOffset, // Skew offset 
	int weight, // Relative weight of right COLORREF
	COLORREF clrBack // Background color
)
{
	pLockIn->RenderLine(pLineIn, y, 0, cx);

	SkewLine(pLineOut, pLineIn, cx, cxOut, iOffset, weight, clrBack);

	pLockOut->SetLine(pLineOut, y, 0, cxOut);
}


// Skews a column vertically (with filtered weights)
// Limited to 45 degree skewing only. Filters two adjacent COLORREFs.
//
// A column cannot be read with GetLine, so this one stays per pixel -- but both
// pages here are PF32, so the caller passes a concrete lock and the access
// inlines to a load and a store.
template<class TLockIn, class TLockOut>
static void VerticalSkew(
	const TLockIn* pLockIn,
	TLockOut* pLockOut,
	COLORREF* pLineIn,
	COLORREF* pLineOut,
	const int cy, // Source height
	const int cyOut, // Destination height
	unsigned x, // Column index
	int iOffset, // Skew offset 
	int weight, // Relative weight of right COLORREF
	COLORREF clrBack // Background color
)
{
	for (int y = 0; y < cy; y++)
	{
		pLineIn[y] = pLockIn->GetPixel(x, y);
	}

	SkewLine(pLineOut, pLineIn, cy, cyOut, iOffset, weight, clrBack);

	for (int y = 0; y < cyOut; y++)
	{
		pLockOut->SetPixel(x, y, pLineOut[y]);
	}
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool RotatePage(IW::Image& imageOut, const IW::Page& pageIn, const double dRadAngle, const double dTan,
                const double dSinE, const COLORREF clrBack, IW::IStatus* pStatus)
{
	const int cx = pageIn.GetWidth();
	const int cy = pageIn.GetHeight();

	const int cx1 = cx + static_cast<int>(double(cy) * fabs(dTan) + 0.5);
	const int cy1 = cy;

	// Calc first shear (horizontal) destination image dimensions 
	IW::Image image1;
	IW::Page& page1 = image1.CreatePage(cx1, cy1, IW::PixelFormat::PF32);

	if (pStatus->QueryCancel()) return false;

	// Perform 1st shear (horizontal) 
	{
		IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
		IW::SurfaceLock<IW::PixelFormat::PF32> lock1(page1);

		IW::CBuffer<COLORREF> pLineIn(cx);
		IW::CBuffer<COLORREF> pLineOut(cx1);

		for (int u = 0; u < cy1; u++)
		{
			const double dShear = (dTan >= 0.0) ? ((u + 0.5) * dTan) : (((u - cy1) + 0.5) * dTan);
			const int iShear = static_cast<int>(floor(dShear));
			const int nWeight = static_cast<int>(255 * (dShear - iShear) + 1) & 0xFF;

			HorizontalSkew(pLockIn.p, &lock1, pLineIn, pLineOut, cx, cx1, u, iShear, nWeight, clrBack);
		}
	}

	// Perform 2nd shear  (vertical)		
	const int cx2 = cx1;
	const int cy2 = static_cast<int>((cx * fabs(dSinE)) + (cy * cos(dRadAngle)) + 0.5) + 1;

	IW::Image image2;
	IW::Page& page2 = image2.CreatePage(cx2, cy2, IW::PixelFormat::PF32);

	// Variable skew offset
	double dOffset2 = (dSinE > 0.0) ? ((cx - 1) * dSinE) : (-dSinE * (cx - cx2));

	if (pStatus->QueryCancel()) return false;

	{
		IW::SurfaceLock<IW::PixelFormat::PF32> lock1(page1);
		IW::SurfaceLock<IW::PixelFormat::PF32> lock2(page2);

		IW::CBuffer<COLORREF> pLineIn(cy1);
		IW::CBuffer<COLORREF> pLineOut(cy2);

		for (int u = 0; u < cx2; u++, dOffset2 -= dSinE)
		{
			const int iShear = static_cast<int>(floor(dOffset2));
			const int nWeight = static_cast<int>(255 * (dOffset2 - iShear) + 1) & 0xFF;

			VerticalSkew(&lock1, &lock2, pLineIn, pLineOut, cy1, cy2, u, iShear, nWeight, clrBack);
		}
	}

	// Perform 3rd shear (horizontal) 	
	const int cx3 = static_cast<int>((cy * fabs(dSinE)) + (cx * cos(dRadAngle)) + 0.5) + 1;
	const int cy3 = cy2;

	// Not the CreatePage(cx, cy, pf) overload: that frees the destination, which
	// on a multi-page image throws away the pages already rotated.
	IW::Page& pageOut = imageOut.CreatePage(CRect(0, 0, cx3, cy3), IW::PixelFormat::PF24);

	double dOffset3 = (dSinE >= 0.0)
		                  ? ((cx - 1) * dSinE * -dTan)
		                  : (dTan * ((cx - 1) * -dSinE + (1 - cy3)));

	if (pStatus->QueryCancel()) return false;

	{
		IW::SurfaceLock<IW::PixelFormat::PF32> lock2(page2);
		IW::SurfaceLock<IW::PixelFormat::PF24> lockOut(pageOut);

		IW::CBuffer<COLORREF> pLineIn(cx2);
		IW::CBuffer<COLORREF> pLineOut(cx3);

		for (int u = 0; u < cy3; u++, dOffset3 += dTan)
		{
			const int iShear = static_cast<int>(floor(dOffset3));
			const int nWeight = static_cast<int>(255 * (dOffset3 - iShear) + 1) & 0xFF;

			HorizontalSkew(&lock2, &lockOut, pLineIn, pLineOut, cx2, cx3, u, iShear, nWeight, clrBack);
		}
	}

	pageOut.CopyExtraInfo(pageIn);
	return true;
}


bool IW::Rotate(const Image& imageInRaw, Image& imageOut, float fAngle, IStatus* pStatus)
{
	// Get it into the positive
	//fAngle = -fAngle;
	while (fAngle > 360.0f) fAngle -= 360.0f;
	while (fAngle < 0.0f) fAngle += 360.0f;

	if (fAngle == 0.0)
	{
		imageOut = imageInRaw;
		return true;
	}

	Image imageIn = imageInRaw;

	// If we are multiple pages then do the render
	if (imageIn.NeedRenderForDisplay())
	{
		Image imageTemp;
		if (!imageIn.Render(imageTemp)) return false;
		imageIn = imageTemp;
	}

	if (fAngle > 45.0 && fAngle <= 135.0)
	{
		// Angle in (45.0 .. 135.0] 
		// Rotate image by 90 degrees into temporary image,
		// so it requires only an extra rotation angle 
		// of -45.0 .. +45.0 to complete rotation.
		fAngle -= 90.0;

		Image imageTemp;
		if (!Rotate90(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}
	else if (fAngle > 135.0 && fAngle <= 225.0)
	{
		// Angle in (135.0 .. 225.0] 
		// Rotate image by 180 degrees into temporary image,
		// so it requires only an extra rotation angle 
		// of -45.0 .. +45.0 to complete rotation.		
		fAngle -= 180.0;

		Image imageTemp;
		if (!Rotate180(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}
	else if (fAngle > 225.0 && fAngle <= 315.0)
	{
		// Angle in (225.0 .. 315.0] 
		// Rotate image by 270 degrees into temporary image,
		// so it requires only an extra rotation angle 
		// of -45.0 .. +45.0 to complete rotation.
		fAngle -= 270.0;

		Image imageTemp;
		if (!Rotate270(imageIn, imageTemp, pStatus)) return false;
		imageIn = imageTemp;
	}

	// If we got here, angle is in (-45.0 .. +45.0]
	constexpr double ROTATE_PI = 3.1415926535897932384626433832795;
	const double dRadAngle = fAngle * ROTATE_PI / 180.0; // Angle in radians
	const double dSinE = sin(dRadAngle);
	const double dTan = tan(dRadAngle / 2.0);
	const COLORREF clrBack = RGBA(0, 0, 0, 0);

	// Rotate each page
	//IW::Image imageRotated;
	for (Image::PageList::const_iterator pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		if (pStatus->QueryCancel()) return false;
		if (!RotatePage(imageOut, *pageIn, dRadAngle, dTan, dSinE, clrBack, pStatus))
			return false;
	}

	/*CRect rectRotated = imageRotated.GetBoundingRect();
	CRect rectIn = imageIn.GetBoundingRect();

	const CRect rectCrop = CalcCropRect(rectIn.Width(), rectIn.Height(), rectRotated.Width(), rectRotated.Height(), dRadAngle);
	if (!IW::Crop(imageRotated, imageOut, rectCrop, pStatus))
		return false;*/

	IterateImageMetaData(imageIn, imageOut, pStatus);

	return true;
}


