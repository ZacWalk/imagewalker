// ImageWalker by Zac Walker
//
// Purpose: TIFF implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "FileTiff.h"
#include "LibfilesDocument.h"
#include "MetadataIPTC.h"
#include <libiw/Diagnostics.h>
#include <libfiles/scan.h>

#ifdef COMPILE_TIFF

/////////////////////////////////////////////////////////////////////
// libtiff 4 provides _TIFFmalloc and friends itself; the app's own copies
// collided with tif_win32.obj at link time.

// Sanity ceiling on a single strip; the directory that supplies the size is
// attacker-controlled.
static constexpr tmsize_t kMaxTiffStripBytes = 256 * 1024 * 1024;


/////////////////////////////////////////////////////////////////////
// FILE Read Proc functions for the tiff lib
/////////////////////////////////////////////////////////////////////


static toff_t _TiffSeekProc(thandle_t fd, toff_t off, int whence)
{
	auto pStreamIn = reinterpret_cast<IW::IStreamIn*>(fd);
	IW::IStreamCommon::ePosition pos;

	switch (whence)
	{
	case SEEK_CUR:
		pos = IW::IStreamCommon::eCurrent;
		break;
	case SEEK_END:
		pos = IW::IStreamCommon::eEnd;
		break;
	case SEEK_SET:
	default:
		pos = IW::IStreamCommon::eBegin;
		break;
	}

	const auto offset = static_cast<std::int64_t>(off);
	if (offset < LONG_MIN || offset > LONG_MAX) return static_cast<toff_t>(-1);
	const DWORD result = pStreamIn->Seek(pos, static_cast<LONG>(offset));
	return result == INVALID_SET_FILE_POINTER ? static_cast<toff_t>(-1) : result;
}

static int _TiffCloseProc(thandle_t fd)
{
	return 0;
}

static toff_t _TiffSizeProc(thandle_t fd)
{
	auto pStream = reinterpret_cast<IW::IStreamIn*>(fd);
	return pStream->GetFileSize();
}

static int _TiffDummyMapProc(thandle_t fd, tdata_t* pbase, toff_t* psize)
{
	return (0);
}

static void _TiffDummyUnmapProc(thandle_t fd, tdata_t base, toff_t size)
{
}

/////////////////////////////////////////////////////////////////////
// FILE Write Proc functions for the tiff lib
/////////////////////////////////////////////////////////////////////

static tsize_t _WriteReadProc(thandle_t fd, tdata_t pBuf, tsize_t size)
{
	return 0;
}

static tsize_t _WriteWriteProc(thandle_t fd, tdata_t pBuf, tsize_t size)
{
	auto pStreamOut = static_cast<IW::IStreamOut*>(fd);

	DWORD dwWrite;
	pStreamOut->Write(pBuf, static_cast<DWORD>(size), &dwWrite);

	return dwWrite;
};


/////////////////////////////////////////////////////////////////////

static uint16 CheckPhotometric(const IW::Page pageIn, uint16 nBitsperSample)
{
	RGBQUAD* rgb;
	uint16 i;

	switch (nBitsperSample)
	{
	case 1:
		{
			rgb = (RGBQUAD*)pageIn.GetPalette();

			if ((rgb->rgbRed == 0) && (rgb->rgbGreen == 0) && (rgb->rgbBlue == 0))
			{
				rgb++;
				if ((rgb->rgbRed == 255) && (rgb->rgbGreen == 255) && (rgb->rgbBlue == 255))
				{
					return PHOTOMETRIC_MINISBLACK;
				}
			}
			if ((rgb->rgbRed == 255) && (rgb->rgbGreen == 255) && (rgb->rgbBlue == 255))
			{
				rgb++;
				if ((rgb->rgbRed == 0) && (rgb->rgbGreen == 0) && (rgb->rgbBlue == 0))
				{
					return PHOTOMETRIC_MINISWHITE;
				}
			}

			return PHOTOMETRIC_PALETTE;
		}

	case 4: // Check if the DIB has a color or a greyscale palette
	case 8:
		rgb = (RGBQUAD*)pageIn.GetPalette();

		for (i = 0; i < (1 << nBitsperSample); i++)
		{
			if ((rgb->rgbRed != rgb->rgbGreen) || (rgb->rgbRed != rgb->rgbBlue))
			{
				return PHOTOMETRIC_PALETTE;
			}
			// The DIB has a color palette if the greyscale isn't a linear ramp
			if (rgb->rgbRed != i)
			{
				return PHOTOMETRIC_PALETTE;
			}

			rgb++;
		}

		return PHOTOMETRIC_MINISBLACK;

	case 16:
	case 24:
	case 32:
		return PHOTOMETRIC_RGB;
	}


	return PHOTOMETRIC_MINISBLACK;
}


extern "C" {
void _TiffWarningHandlerFunc(const char* module, const char* fmt, va_list args)
{
	// Fixed buffer, no formatting allocation: this runs on worker threads inside
	// libtiff's C frames, where a thrown CAtlException would be undefined.
	char sz[512] = {0};
	_vsnprintf_s(sz, _TRUNCATE, fmt, args);
	wchar_t message[600]{};
	_snwprintf_s(message, _TRUNCATE, L"tiff %hs: %hs", module ? module : "TIFFLIB", sz);
	iw::imaging::report_diagnostic(message);
}


void _TiffErrorHandlerFunc(const char* module, const char* fmt, va_list args)
{
	char sz[512] = {0};
	_vsnprintf_s(sz, _TRUNCATE, fmt, args);
	wchar_t message[600]{};
	_snwprintf_s(message, _TRUNCATE, L"tiff %hs: %hs", module ? module : "TIFFLIB", sz);
	iw::imaging::report_diagnostic(message);
}
}

// libtiff 4 owns _TIFFwarningHandler/_TIFFerrorHandler, so these are installed
// through the API rather than by defining the globals.
static void InstallTiffHandlers()
{
	TIFFSetWarningHandler(_TiffWarningHandlerFunc);
	TIFFSetErrorHandler(_TiffErrorHandlerFunc);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadTiff::CLoadTiff()
{
}

CLoadTiff::~CLoadTiff()
{
}


#define CVT(x)      (((x) * 255L) / ((1L<<16)-1))
#define	SCALE(x)	(((x)*((1L<<16)-1))/255)

// The IPTC records among the image resources a TIFF's Photoshop tag holds, found
// by libfiles as Diffractor finds them: the first IPTC resource that holds
// anything, reached by walking the resources rather than by searching the tag
// for its signature.
static void WriteXmpProfile(TIFF* tiff, const IW::MetaData& xmp)
{
	unsigned size = xmp.GetDataSize();

	if (size > 0)
	{
		TIFFSetField(tiff, TIFFTAG_XMLPACKET, size, xmp.GetData());
	}
}


static void WriteIptcProfile(TIFF* tiff, LPCBYTE pBytes, DWORD dwSize)
{
	if (dwSize > 0)
	{
		//Handle TIFFTAG_PHOTOSHOP tag.
		DWORD nLength = dwSize;
		DWORD roundup = (nLength & 0x01); // round up for Photoshop
		IW::CBuffer<BYTE> profile(nLength + roundup + 32);
		IW::MemCopy(profile, "8BIM\04\04\0\0", 8);

		profile[8] = static_cast<BYTE>((nLength >> 24) & 0xff);
		profile[9] = static_cast<BYTE>((nLength >> 16) & 0xff);
		profile[10] = static_cast<BYTE>((nLength >> 8) & 0xff);
		profile[11] = static_cast<BYTE>(nLength & 0xff);

		IW::MemCopy(profile + 12, pBytes, dwSize);

		if (roundup)
			profile[nLength + roundup + 11] = 0;

		TIFFSetField(tiff, TIFFTAG_PHOTOSHOP, static_cast<uint32>(nLength) + roundup + 12, static_cast<void*>(profile));
	}
}

bool CLoadTiff::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::tiff,
		GetKey(), input, output, status);
}

bool CLoadTiff::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn,
                      const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IW::Text::ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	// handle standard nWidth/nHeight/bpp stuff
	m_hTiffOut = nullptr;


	InstallTiffHandlers();

	if (!(m_hTiffOut = TIFFClientOpen("MemSource", "w", pStreamOut,
	                                  _WriteReadProc, _WriteWriteProc, _TiffSeekProc, _TiffCloseProc,
	                                  _TiffSizeProc, _TiffDummyMapProc, _TiffDummyUnmapProc)))
	{

		return false;
	}

	int nPageCount = imageIn.GetPageCount();
	int nPage = 0;

	// multi-paging
	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		nPage++;

		uint32 nHeight;
		uint32 nWidth;
		uint32 nRowsPerStrip = (1024 * 8) / IW::Max(1, pageIn->GetWidth() * pageIn->GetPixelFormat().ToBpp() / 8);

		uint16 nBitsperSample;
		uint16 nSamplesPerpixel = 1;
		uint16 nPhotometric;
		uint16 nCompression;

		uint32 x, y;
		tsize_t nLineBytes;
		uint8* pBuf;

		IW::PixelFormat pf = pageIn->GetPixelFormat();

		nWidth = pageIn->GetWidth();
		nHeight = pageIn->GetHeight();


		switch (pf._pf)
		{
		case IW::PixelFormat::PF1:
		case IW::PixelFormat::PF2:
		case IW::PixelFormat::PF4:
		case IW::PixelFormat::PF8:
		case IW::PixelFormat::PF8Alpha:
		case IW::PixelFormat::PF8GrayScale:
			nSamplesPerpixel = 1;
			nBitsperSample = static_cast<uint16>(pf.ToBpp());
			break;

		case IW::PixelFormat::PF555:
		case IW::PixelFormat::PF565:
			nBitsperSample = 5;
			nSamplesPerpixel = 3;
			break;

		case IW::PixelFormat::PF24:
			nBitsperSample = 8;
			nSamplesPerpixel = 3;
			break;

		case IW::PixelFormat::PF32:
		case IW::PixelFormat::PF32Alpha:
			nBitsperSample = 8;
			nSamplesPerpixel = 4;
			break;

		default:
			assert(0);
			// Unknown depth??
			return false;
		}


		nPhotometric = CheckPhotometric(*pageIn, nBitsperSample * nSamplesPerpixel);

		TIFFCreateDirectory(m_hTiffOut);

		imageIn.IterateMetaData(this);

		TIFFSetField(m_hTiffOut, TIFFTAG_IMAGEWIDTH, nWidth);
		TIFFSetField(m_hTiffOut, TIFFTAG_IMAGELENGTH, nHeight);
		TIFFSetField(m_hTiffOut, TIFFTAG_SAMPLESPERPIXEL, nSamplesPerpixel);
		TIFFSetField(m_hTiffOut, TIFFTAG_BITSPERSAMPLE, nBitsperSample);
		TIFFSetField(m_hTiffOut, TIFFTAG_PHOTOMETRIC, nPhotometric);
		TIFFSetField(m_hTiffOut, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG); // single image plane
		TIFFSetField(m_hTiffOut, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
		TIFFSetField(m_hTiffOut, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(m_hTiffOut, IW::Max(1u, nRowsPerStrip)));

		bool bHasAlpha = pf.HasAlpha();
		if (bHasAlpha)
		{
			// EXTRASAMPLES is a counted array: libtiff reads a count *and* a pointer.
			const uint16 extra = EXTRASAMPLE_ASSOCALPHA;
			TIFFSetField(m_hTiffOut, TIFFTAG_EXTRASAMPLES, static_cast<uint16>(1), &extra);
		}


		// handle metrics
		TIFFSetField(m_hTiffOut, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
		TIFFSetField(m_hTiffOut, TIFFTAG_XRESOLUTION, static_cast<float>(IW::MeterToInch(imageIn.GetXPelsPerMeter())));
		TIFFSetField(m_hTiffOut, TIFFTAG_YRESOLUTION, static_cast<float>(IW::MeterToInch(imageIn.GetYPelsPerMeter())));

		char szPageName[20];
		sprintf_s(szPageName, countof(szPageName), "Page %d", nPage + 1);

		TIFFSetField(m_hTiffOut, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
		TIFFSetField(m_hTiffOut, TIFFTAG_OSUBFILETYPE, OFILETYPE_IMAGE);
		TIFFSetField(m_hTiffOut, TIFFTAG_PAGENUMBER, static_cast<uint16>(nPage + 1), static_cast<uint16>(nPageCount));
		TIFFSetField(m_hTiffOut, TIFFTAG_PAGENAME, szPageName);


		// palettes (image colormaps are automatically scaled to 16-pBits)
		if (nPhotometric == PHOTOMETRIC_PALETTE)
		{
			uint16 *r, *g, *b;
			uint16 nColors = (1 << pageIn->GetPixelFormat().ToBpp());
			auto pPal = (RGBQUAD*)pageIn->GetPalette();

			r = static_cast<uint16*>(_TIFFmalloc(sizeof(uint16) * 3 * nColors));
			g = r + nColors;
			b = g + nColors;

			for (int i = nColors - 1; i >= 0; i--)
			{
				r[i] = SCALE(static_cast<uint16>(pPal[i].rgbRed));
				g[i] = SCALE(static_cast<uint16>(pPal[i].rgbGreen));
				b[i] = SCALE(static_cast<uint16>(pPal[i].rgbBlue));
			}

			TIFFSetField(m_hTiffOut, TIFFTAG_COLORMAP, r, g, b);

			_TIFFfree(r);
		}

		// nCompression

		switch (nBitsperSample * nSamplesPerpixel)
		{
		case 1:
			nCompression = COMPRESSION_CCITTFAX4;
			break;

		case 4:
		case 8:
		case 16:
		case 24:
		case 32:
			nCompression = COMPRESSION_PACKBITS;
			break;

		default:
			nCompression = COMPRESSION_NONE;
			break;
		}

		TIFFSetField(m_hTiffOut, TIFFTAG_COMPRESSION, nCompression);

		nLineBytes = nWidth * nSamplesPerpixel;

		if (TIFFScanlineSize(m_hTiffOut) > nLineBytes)
		{
			nLineBytes = TIFFScanlineSize(m_hTiffOut);
		}

		pBuf = static_cast<uint8*>(_TIFFmalloc(nLineBytes));

		// read the DIB lines from bottom to top
		// and save them in the TIF
		// -------------------------------------
		const uint8* pBits = nullptr;
		uint8* pBufTemp = nullptr;


		switch (nBitsperSample * nSamplesPerpixel)
		{
		case 1:
		case 4:
		case 8:
			{
				for (y = 0; y < nHeight; y++)
				{
					pBits = pageIn->GetBitmapLine(y);
					TIFFWriteScanline(m_hTiffOut, (LPVOID)pBits, y, 0);
				}
				break;
			}

		case 16:
			{
				for (y = 0; y < nHeight; y++)
				{
					pBits = pageIn->GetBitmapLine(y);
					pBufTemp = pBuf;

					for (x = 0; x < nWidth; x++)
					{
						short c = *((short*)pBits);
						*((short*)pBufTemp) = ((c & 0x7c00) >> 10) |
							(c & 0x03e0) |
							((c & 0x001f) << 10);

						pBufTemp += 2;
						pBits += 2;
					}

					TIFFWriteScanline(m_hTiffOut, pBuf, y, 0);
				}

				break;
			}

		case 24:
			{
				for (y = 0; y < nHeight; y++)
				{
					pBits = pageIn->GetBitmapLine(y);
					pBufTemp = pBuf;

					for (x = 0; x < nWidth; x++)
					{
						pBufTemp[0] = pBits[2];
						pBufTemp[1] = pBits[1];
						pBufTemp[2] = pBits[0];
						pBufTemp += 3;
						pBits += 3;
					}

					TIFFWriteScanline(m_hTiffOut, pBuf, y, 0);
				}

				break;
			}

		case 32:
			{
				for (y = 0; y < nHeight; y++)
				{
					pBits = pageIn->GetBitmapLine(y);
					pBufTemp = pBuf;

					for (x = 0; x < nWidth; x++)
					{
						pBufTemp[0] = pBits[2];
						pBufTemp[1] = pBits[1];
						pBufTemp[2] = pBits[0];
						pBufTemp[3] = pBits[3];
						pBufTemp += 4;
						pBits += 4;
					}

					TIFFWriteScanline(m_hTiffOut, pBuf, y, 0);
				}

				break;
			}

		default:
			assert(0); //Unknown depth?
			break;
		}

		_TIFFfree(pBuf);

		TIFFWriteDirectory(m_hTiffOut);
	}


	TIFFClose(m_hTiffOut);

	return true;
}


bool CLoadTiff::AddMetaDataBlob(const IW::MetaData& data)
{
	DWORD dwSize = data.GetDataSize();

	if (dwSize > 0)
	{
		DWORD dwType = data.GetType();
		LPCBYTE p = data.GetData();

		if (dwType == IW::MetaDataTypes::PROFILE_IPTC)
		{
			// TIFF's tag holds the bare records, so a blob that arrived inside a
			// Photoshop APP13 segment has to be unwrapped first.
			const auto iim = MetadataIPTC::GetIim(data);

			if (!iim.empty())
				WriteIptcProfile(m_hTiffOut, iim.data(), static_cast<DWORD>(iim.size()));
		}
		else if (dwType == IW::MetaDataTypes::PROFILE_XMP)
		{
			WriteXmpProfile(m_hTiffOut, data);
		}
		else if (dwType == IW::MetaDataTypes::PROFILE_ICC && dwSize > 0)
		{
			TIFFSetField(m_hTiffOut, TIFFTAG_ICCPROFILE, dwSize, p);
		}
	}

	return true;
}


#endif // COMPILE_TIFF
