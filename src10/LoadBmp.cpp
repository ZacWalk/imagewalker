// LoadBmp.cpp: implementation of the CLoadBmp class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "LoadBmp.h"
#include "dibthumb.h"


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadBmp::CLoadBmp()
{
}

CLoadBmp::~CLoadBmp()
{
}


BOOL CLoadBmp::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	// Read the file header to get the file size and to
	// find out where the bits start in the 
	auto pBmpFileHdr = (BITMAPFILEHEADER*)pByte;

	// Dib Header
	auto pInfo = (BITMAPINFO*)(pByte + sizeof(BITMAPFILEHEADER));

	if (nSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
		return FALSE;

	// Check that we got a real Windows DIB 
	if (pInfo->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
		return FALSE;

	// I only want single plain headers 
	if (pInfo->bmiHeader.biPlanes != 1)
		return FALSE;

	int nBpp = pInfo->bmiHeader.biBitCount;
	int nWidth = pInfo->bmiHeader.biWidth;
	int nHeight = pInfo->bmiHeader.biHeight;

	if (nBpp != 1 && nBpp != 4 && nBpp != 8 && nBpp != 24 && nBpp != 32)
		return FALSE;

	if (nWidth <= 0 || nHeight <= 0)
		return FALSE;

	int nStorageWidth = CalcStorageWidth(nWidth, nBpp);
	RGBQUAD* pRgb = pInfo->bmiColors;

	// bfOffBits and the dimensions are all file-supplied: everything below
	// indexes the mapped view with them.
	{
		const DWORD nPaletteBytes = (nBpp <= 8) ? ((1u << nBpp) * sizeof(RGBQUAD)) : 0;
		// BI_BITFIELDS puts three masks where the palette would be.
		const DWORD nMaskBytes = (pInfo->bmiHeader.biCompression == BI_BITFIELDS) ? (3 * sizeof(DWORD)) : 0;
		const ULONGLONG nBits = static_cast<ULONGLONG>(nStorageWidth) * nHeight;

		if (nStorageWidth <= 0)
			return FALSE;

		if (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + nPaletteBytes + nMaskBytes > nSize)
			return FALSE;

		if (pBmpFileHdr->bfOffBits > nSize || nBits > nSize - pBmpFileHdr->bfOffBits)
			return FALSE;

		// GDI reads biSizeImage bytes for a compressed DIB, which is not the
		// uncompressed size checked above.
		if (pInfo->bmiHeader.biCompression != BI_RGB &&
			pInfo->bmiHeader.biCompression != BI_BITFIELDS &&
			(pInfo->bmiHeader.biSizeImage == 0 ||
				pInfo->bmiHeader.biSizeImage > nSize - pBmpFileHdr->bfOffBits))
			return FALSE;
	}

	ASSERT(nHeight > 0);


	if (pInfo->bmiHeader.biCompression != BI_RGB)
	{
		CDib dib;
		//Its probably compressed so set it
		//to a bit map and back.

		if (!dib.Create(nWidth, nHeight, nBpp, TRUE))
			return FALSE;

		// copy over color

		if (nBpp <= 8)
			CopyPalette(dib.GetColor(), pRgb, (1 << nBpp));

		HDC hDC = GetDC(nullptr);

		SetDIBits(hDC, dib.GetHBitmap(), 0, pInfo->bmiHeader.biHeight,
		          pByte + pBmpFileHdr->bfOffBits,
		          (LPBITMAPINFO)&(pInfo->bmiHeader), DIB_RGB_COLORS);

		ReleaseDC(nullptr, hDC);

		if (!bThumb ||
			(IMAGE_X >= nWidth && IMAGE_Y >= nHeight))
		{
			if (!pDib->Create(nWidth, nHeight, nBpp, FALSE))
				return FALSE;

			if (nBpp <= 8)
				CopyPalette(pDib->GetColor(), pRgb, (1 << nBpp));

			CopyMemory(pDib->GetBitmap(),
			           dib.GetBitmap(),
			           dib.GetHeader()->biSizeImage);
		}
		else
		{
			switch (nBpp)
			{
			case 1:
				{
					CIteratorDib<CDibIterator1> d(&dib);
					CDibThumb<CIteratorDib<CDibIterator1>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}

			case 4:
				{
					CIteratorDib<CDibIterator4> d(&dib);
					CDibThumb<CIteratorDib<CDibIterator4>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 8:
				{
					CIteratorDib<CDibIterator8> d(&dib);
					CDibThumb<CIteratorDib<CDibIterator8>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 24:
				{
					CIteratorDib<CDibIterator24> d(&dib);
					CDibThumb<CIteratorDib<CDibIterator24>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 32:
				{
					CIteratorDib<CDibIterator32> d(&dib);
					CDibThumb<CIteratorDib<CDibIterator32>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			default:
				ASSERT(FALSE);
			}
		}
	}
	else
	{
		if ((!bThumb)
			|| (IMAGE_X >= nWidth && IMAGE_Y >= nHeight))
		{
			if (!pDib->Create(nWidth, nHeight, nBpp, FALSE))
				return FALSE;

			if (nBpp <= 8)
				CopyPalette(pDib->GetColor(), pRgb, (1 << nBpp));

			CopyMemory(pDib->GetBitmap(),
			           pByte + pBmpFileHdr->bfOffBits,
			           nStorageWidth * nHeight);
		}
		else
		{
			CSize size(nWidth, nHeight);
			LPBYTE p = pByte + pBmpFileHdr->bfOffBits + nStorageWidth * (nHeight - 1);
			RGBQUAD pRgbWithAlpha[256];

			if (nBpp <= 8)
				CopyPalette(pRgbWithAlpha, pRgb, (1 << nBpp));

			switch (nBpp)
			{
			case 1:
				{
					CIteratorDibFile<CDibIterator1> d(p, pRgbWithAlpha, size, -nStorageWidth);
					CDibThumb<CIteratorDibFile<CDibIterator1>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}

			case 4:
				{
					CIteratorDibFile<CDibIterator4> d(p, pRgbWithAlpha, size, -nStorageWidth);
					CDibThumb<CIteratorDibFile<CDibIterator4>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 8:
				{
					CIteratorDibFile<CDibIterator8> d(p, pRgbWithAlpha, size, -nStorageWidth);
					CDibThumb<CIteratorDibFile<CDibIterator8>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 24:
				{
					CIteratorDibFile<CDibIterator24> d(p, pRgbWithAlpha, size, -nStorageWidth);
					CDibThumb<CIteratorDibFile<CDibIterator24>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			case 32:
				{
					CIteratorDibFile<CDibIterator32> d(p, pRgbWithAlpha, size, -nStorageWidth);
					CDibThumb<CIteratorDibFile<CDibIterator32>> Scale;
					Scale.Scale(*pDib, d);
					break;
				}
			default:
				ASSERT(FALSE);
			}
		}
	}

	pDib->m_strInfo.Format("%dx%dx%d BMP", nWidth, nHeight, nBpp);


	return TRUE;
}

void CLoadBmp::CopyPalette(RGBQUAD* pDst, RGBQUAD* pSrc, UINT uColorEntries)
{
	for (UINT i = 0; i < uColorEntries; i++)
	{
		pDst->rgbBlue = pSrc->rgbBlue;
		pDst->rgbGreen = pSrc->rgbGreen;
		pDst->rgbRed = pSrc->rgbRed;

		pDst->rgbReserved = 0xff;
		pDst++;
		pSrc++;
	}
}
