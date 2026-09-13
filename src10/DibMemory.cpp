// DibMemory.cpp: implementation of the CDib class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "dib.h"

#include <malloc.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


// Create a new DIB
BOOL CDib::Create(unsigned width, unsigned height, unsigned bpp, BOOL bHBitmap)
{
	ASSERT_VALID(this);

	// Before the reuse test below: a recycled DIB must not keep the source size
	// of whatever was in it last.
	m_sizeOriginal = CSize(0, 0);

	if (IsOpen() && Width() == width && Height() == height && bpp == Bpp())
		return TRUE;

	// Delete any existing stuff.
	Free();
	m_uFlags = 0;

	// Allocate memory for the header.

	int iColorEntries = 0;

	if (bpp <= 8)
	{
		iColorEntries = (1 << bpp);
	}

	// Calc storage width;

	m_nStorageWidth = CalcStorageWidth(width, bpp);

	// Allocate memory for the bits (DWORD aligned).
	// width/height come from file headers, so do this in 64-bit.

	const unsigned __int64 nSizeImage64 =
		static_cast<unsigned __int64>(m_nStorageWidth) * height;

	if (width == 0 || height == 0 || m_nStorageWidth <= 0 || nSizeImage64 > (256ui64 << 20))
	{
		TRACE("Refusing an implausible DIB size");
		return FALSE;
	}

	int nSizeImage = static_cast<int>(nSizeImage64);
	int nSizeHeader = sizeof(BITMAPINFOHEADER) + iColorEntries * sizeof(RGBQUAD);

	m_pBMI = (BITMAPINFO*)new BYTE[nSizeHeader + nSizeImage + 4];

	if (!m_pBMI)
	{
		TRACE("Out of memory for DIB header");
		return FALSE;
	}

	// Fill in the header info.
	auto pBI = (BITMAPINFOHEADER*)m_pBMI;
	pBI->biSize = sizeof(BITMAPINFOHEADER);
	pBI->biWidth = width;
	pBI->biHeight = height;
	pBI->biPlanes = 1;
	pBI->biBitCount = bpp;
	pBI->biCompression = BI_RGB;
	pBI->biSizeImage = nSizeImage;
	pBI->biXPelsPerMeter = 0;
	pBI->biYPelsPerMeter = 0;
	pBI->biClrUsed = iColorEntries;
	pBI->biClrImportant = 0;

	if (bpp <= 8)
	{
		// Create an arbitrary color table (gray scale).
		RGBQUAD* prgb = GetColor();

		for (int i = 0; i < iColorEntries; i++)
		{
			prgb->rgbBlue = prgb->rgbGreen = prgb->rgbRed = static_cast<BYTE>(i);

			prgb->rgbReserved = 0xff;
			prgb++;
		}
	}

	if (bHBitmap)
	{
		// NULL reference DC: this runs on the loader threads too, and the DC
		// was taken on the UI thread's main window.
		m_hBitmap = CreateDIBSection(nullptr, m_pBMI, DIB_RGB_COLORS, (LPVOID*)&m_pBits, nullptr, NULL);
	}
	else
	{
		m_pBits = (LPBYTE)m_pBMI + nSizeHeader;
	}

	return (m_pBits != nullptr);
}

/*int CDib::GetPixel(unsigned x, unsigned y)
{
   ASSERT_VALID(this);
   ASSERT(m_pBits);
   
   switch (Bpp())
   {
   case 1:
      return GetBitmap(y)[x / 8] & (1 << (7-(x & 0x7))) ? 1 : 0;
   case 2:
      ASSERT(0);
      return 1;
   case 4:
      if (x & 1)
         return GetBitmap(y)[x / 2] & 0x0f;
      else
         return (GetBitmap(y)[x / 2] & 0xf0) >> 4;
   case 8:
      return GetBitmap(y)[x];
      
   case 16:
      return ((WORD*)GetBitmap(y))[x];
      
   case 24:
      {
         BYTE *p = GetBitmap(y) + x*3;
         int i = *p++;
         i += *p++ << 8;
         i += *p++ << 16;
         
         return i;
      }
      
   default:
      break;
   };
   
   ASSERT(0);
   return 1;
}      

void CDib::SetPixel(unsigned x, unsigned y, unsigned c)
{
   ASSERT_VALID(this);
   ASSERT(m_pBits);
   
   switch (Bpp())
   {
   case 1:
      {
         BYTE mask = (1 << (7-(x & 0x7)));
         
         if (c)
            GetBitmap(y)[x / 8] |= mask;
         else
            GetBitmap(y)[x / 8] &= (mask ^ 0xff);
      }
      break;
   case 2:
      ASSERT(0);
      break;
   case 4:
      if (x & 1)
         GetBitmap(y)[x / 2] = (GetBitmap(y)[x / 2] & 0xf0) | c;
      else
         GetBitmap(y)[x / 2] = (GetBitmap(y)[x / 2] & 0x0f) | (c << 4);
      break;
   case 8:
      GetBitmap(y)[x] = c;
      break;
   case 16:
      ((WORD*)GetBitmap(y))[x] = (WORD)c;
      break;
      
   case 24:
      {
         BYTE *p = GetBitmap(y) + x*3;
         *p++ = c;
         *p++ = c>>8;
         *p++ = c>>16;
      }
      break;
   default:
      ASSERT(0);
      break;
   }
   return;
}      */


/*RGBQUAD CDib::GetPixelColor(int x, int y)
{
   ASSERT_VALID(this);
   BYTE *p = GetBitmap(y);
   
   RGBQUAD rgb = {0,0,0,0};
   
   switch (Bpp())
   {
   case 1:
      rgb = GetColor()[GetPixel(x, y)];
      break;
   case 4:
      rgb = GetColor()[GetPixel(x, y)];
      break;
   case 8:
      rgb = GetColor()[p[x]];
      break;
   case 16:
      ASSERT(0);
      break;
   case 24:
      rgb.rgbRed = p[x*3+2];
      rgb.rgbGreen = p[x*3+1];
      rgb.rgbBlue = p[x*3];
      break;
   case 32:
      rgb = ((RGBQUAD*)p)[x];
      break;
   default:
      ASSERT(0);
      break;
   }
   
   return rgb;
   
}*/

/*BOOL CDib::GetRgbRow(BYTE * pLine, int y, int nStart, int nStop)
{
   ASSERT_VALID(this);
   if (nStop == 0)
      nStop = Width();
   
   ASSERT(nStart < nStop);
   
   if (Bpp() == 32)
   {
      BYTE *pSrcLine = GetBitmap(y) + nStart*4;
      for(int x = nStart; x < nStop; x++)
      {
         *pLine++ = pSrcLine[2];
         *pLine++ = pSrcLine[1];
         *pLine++ = pSrcLine[0];
         
         pSrcLine += 4;
      }
   } else if (Bpp() == 24)
   {
      BYTE *pSrcLine = GetBitmap(y) + nStart*3;
      for(int x = nStart; x < nStop; x++)
      {
         *pLine++ = pSrcLine[2];
         *pLine++ = pSrcLine[1];
         *pLine++ = pSrcLine[0];
         
         pSrcLine += 3;
      }
   }
   else
   {
      for(int x = nStart; x < nStop; x++)
      {
         RGBQUAD rgb = GetPixelColor(x,y);
         
         *pLine++ = rgb.rgbRed;
         *pLine++ = rgb.rgbGreen;
         *pLine++ = rgb.rgbBlue;
      }
   }
   
   return TRUE;
}

BOOL CDib::GetRgbCol(BYTE * pLine, int x, int nStart, int nStop)
{
   ASSERT_VALID(this);

   if (nStop == 0)
      nStop = Height();
   
   ASSERT(nStart < nStop);
   
   for(int y = nStart; y < nStop; y++)
   {
      RGBQUAD rgb = GetPixelColor(x,y);
      
      *pLine++ = rgb.rgbRed;
      *pLine++ = rgb.rgbGreen;
      *pLine++ = rgb.rgbBlue;
   }
   
   return TRUE;
}

void CDib::Set(CDib * pDib)
{
   ASSERT_VALID(this);
   Free();
   
   if (pDib->IsOpen())
   {
      
      m_pBMI = pDib->m_pBMI;
      m_pBits = pDib->m_pBits;
      m_hBitmap = pDib->m_hBitmap;
      m_nStorageWidth = pDib->m_nStorageWidth;
      m_nOriginalType = pDib->m_nOriginalType;
#ifdef _DEBUG
      m_nAllocLength = pDib->m_nAllocLength;
#endif // _DEBUG
      
      pDib->m_pBMI = NULL;
      pDib->m_pBits = NULL;
      pDib->m_hBitmap = NULL;
   }
}*/

void CDib::Free()
{
	ASSERT_VALID(this);

	if (m_hBitmap)
	{
		DeleteObject(m_hBitmap);
		m_hBitmap = nullptr;
	}

	if (m_pBMI)
		delete[] (BYTE*)m_pBMI;

	m_pBMI = nullptr;
	m_pBits = nullptr;

	m_nStorageWidth = 0;
	m_nOriginalType = DIB_UNKNOWN;
	m_uFlags = 0;
	m_sizeOriginal = CSize(0, 0);
}
