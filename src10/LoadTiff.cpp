// LoadTiff.cpp: implementation of the CLoadTiff class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "iw/channelaverage.h"
#include "artmate.h"
#include "LoadTiff.h"

#include "DibThumb.h"
#include "iw/logfile.h"

#ifdef _DEBUG
#undef THIS_FILE
char THIS_FILE[] = __FILE__;
#endif

extern "C" {
#include "tiffiop.h"
}

// These run on the thumbnail worker, so they log and return. The error handler
// used to put up a modal MessageBox: one Canon raw file in a folder (a CR2 is a
// TIFF using old-style JPEG, which libtiff refuses) stopped the whole thumbnail
// run behind "Old-style JPEG compression support is not configured".
void CLoadTiff::MyTiffWarningHandler(const char* module, const char* fmt, va_list ap)
{
	TiffDiagnostic(_T("warning"), module, fmt, ap);
}


void CLoadTiff::MyTiffErrorHandler(const char* module, const char* fmt, va_list ap)
{
	TiffDiagnostic(_T("error"), module, fmt, ap);
}

void CLoadTiff::TiffDiagnostic(LPCTSTR szKind, const char* module, const char* fmt, va_list ap)
{
	char szText[512] = {0};
	_vsnprintf_s(szText, sizeof(szText), _TRUNCATE, fmt, ap);

	IW::Logging::Warn(_T("libtiff %s in %hs: %hs"), szKind,
	                  module != nullptr ? module : "TIFFLIB", szText);
}

/* check if color map holds old-style 8-bit values */
int CLoadTiff::Checkcmap(int n, uint16* r, uint16* g, uint16* b)
{
	while (n-- > 0)
		if (*r++ >= 256 || *g++ >= 256 || *b++ >= 256)
			return (16);

	return (8);
}

tsize_t CLoadTiff::MyTiffReadProc(thandle_t fd, tdata_t buf, tsize_t size)
{
	auto pInfo = static_cast<CLoadTiff*>(fd);

	if (pInfo->m_nPos >= pInfo->m_nSize)
		return 0;

	/* Make sure we don't run over the end of the file */
	if (size + pInfo->m_nPos > pInfo->m_nSize)
		size = pInfo->m_nSize - pInfo->m_nPos;

	CopyMemory(buf, pInfo->m_pData+pInfo->m_nPos, size);
	pInfo->m_nPos += static_cast<UINT>(size);

	ASSERT(pInfo->m_nPos <= pInfo->m_nSize);

	return size;
}

tsize_t CLoadTiff::MyTiffWriteProc(thandle_t fd, tdata_t buf, tsize_t size)
{
	ASSERT(0);

	return 0;
}

toff_t CLoadTiff::MyTiffSeekProc(thandle_t fd, toff_t off, int whence)
{
	auto pInfo = static_cast<CLoadTiff*>(fd);

	switch (whence)
	{
	case 1:
		pInfo->m_nPos += static_cast<UINT>(off);
		break;
	case 2:
		pInfo->m_nPos = static_cast<UINT>(pInfo->m_nSize - off);
		break;
	case 0:
	default:
		pInfo->m_nPos = static_cast<UINT>(off);
		break;
	}

	if (pInfo->m_nPos > pInfo->m_nSize)
		pInfo->m_nPos = pInfo->m_nSize;

	ASSERT(pInfo->m_nPos <= pInfo->m_nSize);

	return pInfo->m_nPos;
}

int CLoadTiff::MyTiffCloseProc(thandle_t fd)
{
	return 0;
}

toff_t CLoadTiff::MyTiffSizeProc(thandle_t fd)
{
	auto pInfo = static_cast<CLoadTiff*>(fd);
	return pInfo->m_nSize;
}

int CLoadTiff::MyTiffDummyMapProc(thandle_t fd, tdata_t* pbase, toff_t* psize)
{
	return (0);
}

void CLoadTiff::MyTiffDummyUnmapProc(thandle_t fd, tdata_t base, toff_t size)
{
}

#define CVT(x)      (((x) * 255L) / ((1L<<16)-1))

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadTiff::CLoadTiff()
{
}

CLoadTiff::~CLoadTiff()
{
}


BOOL CLoadTiff::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	// libtiff 4 owns _TIFFwarningHandler/_TIFFerrorHandler, so these go in
	// through the API rather than by assigning the globals.
	TIFFSetWarningHandler(MyTiffWarningHandler);
	TIFFSetErrorHandler(MyTiffErrorHandler);

	m_pData = pByte;
	m_nSize = nSize;
	m_nPos = 0;

	TIFF* tif = TIFFClientOpen("MemSource", "r", this,
	                           MyTiffReadProc, MyTiffWriteProc,
	                           MyTiffSeekProc, MyTiffCloseProc, MyTiffSizeProc,
	                           MyTiffDummyMapProc, MyTiffDummyUnmapProc);


	if (tif)
	{
		size_t npixels;
		uint32* raster;
		UINT nSrcHeight = 0, nSrcWidth = 0;
		UINT nBpp = 4 * 8;

		if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &nSrcWidth) ||
			!TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &nSrcHeight) ||
			nSrcWidth == 0 || nSrcHeight == 0)
		{
			TIFFClose(tif);
			return FALSE;
		}

		// The dimensions are file-supplied; a 32-bit product wraps.
		const unsigned __int64 nRasterBytes =
			static_cast<unsigned __int64>(nSrcWidth) * nSrcHeight * sizeof(uint32);

		if (nRasterBytes > (512ui64 << 20))
		{
			TIFFClose(tif);
			return FALSE;
		}

		npixels = static_cast<size_t>(nSrcWidth) * nSrcHeight;
		raster = static_cast<uint32*>(_TIFFmalloc(static_cast<tmsize_t>(nRasterBytes)));

		if (raster != nullptr)
		{
			if (TIFFReadRGBAImage(tif, nSrcWidth, nSrcHeight, raster, 0))
			{
				if ((!bThumb) ||
					(IMAGE_X >= nSrcWidth && IMAGE_Y >= nSrcHeight))
				{
					if (pDib->Create(nSrcWidth, nSrcHeight, nBpp, FALSE))
					{
						for (UINT y = 0; y < nSrcHeight; y++)
						CopyMemory(pDib->GetBitmap((nSrcHeight - y) - 1),
						           raster + nSrcWidth * y, nSrcWidth * 4);

						SwapRB(pDib);
					}
				}
				else
				{
					UINT nDstWidth = (nSrcWidth * IMAGE_Y) / nSrcHeight;
					UINT nDstHeight = (nSrcHeight * IMAGE_X) / nSrcWidth;

					if (nDstWidth > IMAGE_X)
					{
						nDstWidth = IMAGE_X;
					}
					else
					{
						nDstHeight = IMAGE_Y;
					}

					// A very wide, very short image rounds one of these to zero,
					// and the row loop below then never terminates.
					if (nDstWidth == 0) nDstWidth = 1;
					if (nDstHeight == 0) nDstHeight = 1;

					UINT uDstRunWidth = nDstWidth * (nBpp / 8 + 1);
					UINT nRunSize = uDstRunWidth * sizeof(UINT);

					auto pRun = (UINT*)GetBuffer(nRunSize + 8);

					ZeroMemory(pRun, nRunSize);

					UINT *pr, *pd = pRun + uDstRunWidth;
					UINT x;
					LPBYTE p;

					if (pDib->Create(nDstWidth, nDstHeight, nBpp, FALSE))
					{
						UINT yc = 0;
						UINT yDst = 0;
						UINT ySrc = 0;
						UINT yy = 0;

						while (ySrc < nSrcHeight)
						{
							yy += nSrcHeight;
							yc = 0;

							while (yy >= nDstHeight)
							{
								x = 0;
								pr = pRun;
								p = (LPBYTE)(raster + nSrcWidth * ySrc);

								while (pr < pd)
								{
									x += nSrcWidth;

									while (x >= nDstWidth)
									{
										pr[0] += p[0];
										pr[1] += p[1];
										pr[2] += p[2];
										pr[3] += p[3];
										pr[4] += 1;

										x -= nDstWidth;
										p += 4;
									}

									pr += 5;
								}
								yy -= nDstHeight;
								yc += 1;
							}

							ySrc += yc;

							p = pDib->GetBitmap((nDstHeight - yDst) - 1);
							pr = pRun;

							while (pr < pd)
							{
								p[0] = IW::ChannelAverage(pr[2], pr[4]);
								p[1] = IW::ChannelAverage(pr[1], pr[4]);
								p[2] = IW::ChannelAverage(pr[0], pr[4]);
								p[3] = IW::ChannelAverage(pr[3], pr[4]);

								p += 4;
								pr += 5;
							}

							ZeroMemory(pRun, nRunSize);

							yDst += 1;
						}
					}
				}
			}

			_TIFFfree(raster);
		}
		TIFFClose(tif);
	}

	return pDib->IsOpen();
}
