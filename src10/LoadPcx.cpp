// LoadPcx.cpp: implementation of the CLoadPcx class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "iw/channelaverage.h"
#include "artmate.h"
#include "LoadPcx.h"
#include "dibthumb.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma pack(1)
using PcxHeader = struct
{
	BYTE manufacturer; /* Always set to 0 */
	BYTE version; /* Always 5 for 256-color files */
	BYTE encoding; /* Always set to 1 */
	BYTE bits_per_pixel; /* Should be 8 for 256-color files */
	short xmin, ymin; /* Coordinates for top left corner */
	short xmax, ymax; /* m_nSrcWidth and nSrcHeight of image */
	short hres; /* Horizontal resolution of image */
	short vres; /* Vertical resolution of image */
	BYTE palette16[48]; /* EGA palette; not used for 256-color files */
	BYTE reserved; /* Reserved for future use */
	BYTE color_planes; /* Color planes */
	short bytes_per_line; /* Number of bytes in 1 line of pixels */
	short palette_type; /* Should be 2 for color palette */
	BYTE filler[58]; /* Nothing but junk */
};
#pragma pack()

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadPcx::CLoadPcx()
{
}

CLoadPcx::~CLoadPcx()
{
}

BOOL CLoadPcx::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	auto pHeader = (PcxHeader*)pByte;

	if (nSize < sizeof(PcxHeader))
		return FALSE;

	LPCBYTE pEnd = pByte + nSize;

	// bits_per_pixel sizes the palette loop below, so it has to be one of the
	// four the DIB can represent before anything else uses it.
	if (pHeader->manufacturer != 0x0a ||
		pHeader->encoding != 1 ||
		pHeader->color_planes != 1 ||
		(pHeader->bits_per_pixel != 1 && pHeader->bits_per_pixel != 2 &&
			pHeader->bits_per_pixel != 4 && pHeader->bits_per_pixel != 8))
	{
		return FALSE;
	}

	pByte += sizeof(PcxHeader);

	m_nSrcWidth = 1 + pHeader->xmax - pHeader->xmin;
	m_nSrcHeight = 1 + pHeader->ymax - pHeader->ymin;

	if (m_nSrcWidth == 0 || m_nSrcHeight == 0)
		return FALSE;

	LPBYTE pRgb = pByte;

	m_nRepCount = 0;
	m_nRepByte = 0;

	UINT nColorMapSize = 1 << (pHeader->bits_per_pixel);

	if (nColorMapSize > 2)
	{
		pRgb = pHeader->palette16;
	}
	else
	{
		static BYTE MonoPal[] = {0, 0, 0, 255, 255, 255};

		pRgb = MonoPal;
	}


	UINT y = 0;
	BOOL bOk = TRUE;

	if ((!bThumb) ||
		(IMAGE_X >= m_nSrcWidth && IMAGE_Y >= m_nSrcHeight))
	{
		if (pDib->Create(m_nSrcWidth, m_nSrcHeight, 8, FALSE))
		{
			LPBYTE p;

			while (bOk && (y < m_nSrcHeight))
			{
				p = pDib->GetBitmap(y);
				bOk = ScanLine(p, pByte, pEnd);
				if (!bOk)
					return FALSE;

				y += 1;
			}

			if (nColorMapSize > 16)
			{
				p = pByte;
				// handle trailing colormap 
				while (p < pEnd && *p++ != 0x0c) {}

				if (p + 3 * nColorMapSize > pEnd)
					return FALSE;
			}
			else
			{
				p = pRgb;
			}

			LPRGBQUAD pRGB = pDib->GetColor();

			for (UINT i = 0; i < nColorMapSize; i++)
			{
				pRGB[i].rgbRed = *p++;
				pRGB[i].rgbGreen = *p++;
				pRGB[i].rgbBlue = *p++;
				pRGB[i].rgbReserved = 0xff;
			}
		}
	}
	else
	{
		UINT nDstWidth = (m_nSrcWidth * IMAGE_Y) / m_nSrcHeight;
		UINT nDstHeight = (m_nSrcHeight * IMAGE_X) / m_nSrcWidth;

		if (nDstWidth > IMAGE_X)
		{
			nDstWidth = IMAGE_X;
		}
		else
		{
			nDstHeight = IMAGE_Y;
		}

		// A very wide, very short image rounds one of these to zero, and the
		// row loop below then never terminates.
		if (nDstWidth == 0) nDstWidth = 1;
		if (nDstHeight == 0) nDstHeight = 1;

		if (nColorMapSize > 16)
		{
			const UINT nMax = m_nSrcWidth * m_nSrcHeight;
			UINT nRepCount = 0;

			pRgb = pByte;
			BYTE b = 0;

			// Skip the RLE payload to find the trailing palette. pRgb walks the
			// mapped file, so every read has to be bounded.
			for (UINT n = 0; n < nMax; n++)
			{
				if (nRepCount == 0)
				{
					if (pRgb >= pEnd)
						return FALSE;

					b = *pRgb++;

					if ((b & 0xC0) == 0xC0)
					{
						// have a rep. count 
						nRepCount = b & 0x3F;

						if (pRgb >= pEnd)
							return FALSE;

						b = *pRgb++;
					}
					else
					{
						nRepCount = 1;
					}

					if (nRepCount == 0)
						return FALSE;
				}

				nRepCount--;
			}

			// handle trailing colormap 
			while (pRgb < pEnd && *pRgb++ != 0x0c) {}

			if (pRgb + 3 * nColorMapSize > pEnd)
				return FALSE;
		}

		if (pDib->Create(nDstWidth, nDstHeight, 32, FALSE))
		{
			UINT uDstRunWidth = nDstWidth * 5;

			UINT nRunSize = uDstRunWidth * sizeof(UINT);
			UINT nLineInSize = m_nSrcWidth;
			UINT nSizeAll = nLineInSize + nRunSize;

			LPBYTE pLineIn = GetBuffer(nSizeAll + 8);
			auto pRun = reinterpret_cast<UINT*>(((reinterpret_cast<UINT_PTR>(pLineIn) + nLineInSize) / 8 + 1) * 8);

			UINT *pr, *pd = pRun + uDstRunWidth;


			ZeroMemory(pLineIn, nSizeAll + 8);

			UINT yDst = 0;
			UINT ySrc = 0;
			UINT x;
			LPBYTE p;

			UINT yy = 0, yc = 0;

			while ((ySrc < m_nSrcHeight) && bOk)
			{
				yy += m_nSrcHeight;
				yc = 0;

				while ((yy >= nDstHeight) && bOk)
				{
					x = 0;
					p = pLineIn;
					pr = pRun;
					bOk = ScanLine(p, pByte, pEnd);
					if (!bOk)
						return FALSE;

					while (pr < pd)
					{
						x += m_nSrcWidth;

						while (x >= nDstWidth)
						{
							// ScanLine yields raw bytes, not depth-masked indices
							LPBYTE pRGB = pRgb + (3 * (*p++ & (nColorMapSize - 1)));

							pr[0] += pRGB[0];
							pr[1] += pRGB[1];
							pr[2] += pRGB[2];
							pr[3] += 0xff;
							pr[4] += 1;

							x -= nDstWidth;
						}

						pr += 5;
					}
					yy -= nDstHeight;
					yc += 1;
				}

				ySrc += yc;

				pr = pRun;
				p = pDib->GetBitmap(yDst);

				while (pr < pd)
				{
					UINT d = pr[4];

					p[0] = IW::ChannelAverage(pr[2], d);
					p[1] = IW::ChannelAverage(pr[1], d);
					p[2] = IW::ChannelAverage(pr[0], d);
					p[3] = IW::ChannelAverage(pr[3], d);

					p += 4;
					pr += 5;
				}

				ZeroMemory(pRun, nRunSize);

				yDst += 1;
			}
		}
	}

	pDib->m_strInfo.Format("%dx%dx%d PCX", m_nSrcWidth, m_nSrcHeight, pHeader->bits_per_pixel * pHeader->color_planes);

	return pDib->IsOpen();
}


BOOL CLoadPcx::ScanLine(LPBYTE pLine, LPBYTE& pByte, LPCBYTE pEnd)
{
	// m_nRepCount persists across rows, so a row can start mid-run; b must carry
	// a defined value into that case.
	BYTE b = m_nRepByte;

	for (UINT x = 0; x < m_nSrcWidth; x++)
	{
		if (m_nRepCount == 0)
		{
			if (pByte >= pEnd)
				return FALSE;

			b = *pByte++;

			if ((b & 0xC0) == 0xC0)
			{
				// have a rep. count 
				m_nRepCount = b & 0x3F;

				if (pByte >= pEnd)
					return FALSE;

				b = *pByte++;
			}
			else
			{
				m_nRepCount = 1;
			}

			// A run count of zero consumes no input but decrements m_nRepCount,
			// which wraps and poisons every following row.
			if (m_nRepCount == 0)
				return FALSE;
		}

		pLine[x] = b;
		m_nRepCount--;
	}

	m_nRepByte = b;
	return TRUE;
}
