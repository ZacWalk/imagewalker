// ImageWalker by Zac Walker
//
// Purpose: TIFF implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "FileTiff.h"
#include "MetadataIPTC.h"
#include "iw/logfile.h"

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


static tsize_t _ReadReadProc(thandle_t fd, tdata_t pBuf, tsize_t size)
{
	auto pStreamIn = reinterpret_cast<IW::IStreamIn*>(fd);

	DWORD dwRead = 0;
	HRESULT hr = pStreamIn->Read(pBuf, static_cast<DWORD>(size), &dwRead);

	//assert(size == dwRead);
	assert(SUCCEEDED(hr));

	if (FAILED(hr))
	{
		throw IW::invalid_file();
	}

	return dwRead;
}

static tsize_t _ReadWriteProc(thandle_t fd, tdata_t pBuf, tsize_t size)
{
	assert(0);

	return 0;
}


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

	return pStreamIn->Seek(pos, static_cast<LONG>(off));
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
	IW::Logging::Warn(_T("tiff %hs: %hs"), module ? module : "TIFFLIB", sz);
}


void _TiffErrorHandlerFunc(const char* module, const char* fmt, va_list args)
{
	char sz[512] = {0};
	_vsnprintf_s(sz, _TRUNCATE, fmt, args);
	IW::Logging::Error(_T("tiff %hs: %hs"), module ? module : "TIFFLIB", sz);
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


static uint16 CheckColormap(int n, uint16* r, uint16* g, uint16* b)
{
	while (n-- > 0)
	{
		if (*r++ >= 256 || *g++ >= 256 || *b++ >= 256)
		{
			return 16;
		}
	}

	return 8;
}

#define CVT(x)      (((x) * 255L) / ((1L<<16)-1))
#define	SCALE(x)	(((x)*((1L<<16)-1))/255)

static void ReadIptcProfile(char* pField, long int nLength, IW::MetaData& dataIPTC)
{
	if (nLength <= 6)
		return;

	auto p = (LPBYTE)pField;

	//  Handle PHOTOSHOP tag. The compare reads six bytes, so it has to stop six
	//  short of the end of the field, not at it.
	while (nLength >= 6)
	{
		if (memcmp(p, "8BIM\04\04", 6) == 0)
			break;

		nLength -= 2;
		p += 2;
	}

	if (nLength <= 12)
		return;

	p += 6;
	nLength -= 6;

	int nLength2 = (*p++);
	nLength--;

	if (nLength2)
	{
		if (nLength2 > nLength)
			return;

		nLength -= nLength2;
		p += nLength2;
	}

	if ((nLength2 & 0x01) == 0)
	{
		if (nLength < 1)
			return;

		p++; /* align to an even byte boundary */
		nLength--;
	}

	if (nLength < 4)
		return;

	nLength2 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	p += 4;
	nLength -= 4;

	if (nLength2 < 0 || nLength2 > nLength)
		return;

	dataIPTC.SetType(IW::MetaDataTypes::PROFILE_IPTC);
	dataIPTC.CopyData(p, nLength2);
}

static void ReadXmpProfile(char* pField, int nLength, IW::MetaData& xmp)
{
	xmp.SetType(IW::MetaDataTypes::PROFILE_XMP);
	xmp.CopyData(pField, nLength);
}

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

static LPCTSTR GetCompressionString(int nCompression)
{
	LPCTSTR szCompression = g_szEmptyString;

	switch (nCompression)
	{
	case COMPRESSION_CCITTRLE:
		szCompression = _T("CCITTRLE");
		break;
	case COMPRESSION_CCITTFAX3:
		szCompression = _T("CCITTFAX3");
		break;
	case COMPRESSION_CCITTFAX4:
		szCompression = _T("CCITTFAX4");
		break;
	case COMPRESSION_LZW:
		szCompression = _T("LZW");
		break;
	case COMPRESSION_OJPEG:
		szCompression = _T("OJPEG");
		break;
	case COMPRESSION_JPEG:
		szCompression = _T("JPEG");
		break;
	case COMPRESSION_NEXT:
		szCompression = _T("NEXT");
		break;
	case COMPRESSION_CCITTRLEW:
		szCompression = _T("CCITTRLEW");
		break;
	case COMPRESSION_PACKBITS:
		szCompression = _T("PACKBITS");
		break;
	case COMPRESSION_THUNDERSCAN:
		szCompression = _T("THUNDERSCAN");
		break;
	case COMPRESSION_IT8CTPAD:
		szCompression = _T("IT8CTPAD");
		break;
	case COMPRESSION_IT8LW:
		szCompression = _T("IT8LW");
		break;
	case COMPRESSION_IT8MP:
		szCompression = _T("IT8MP");
		break;
	case COMPRESSION_IT8BL:
		szCompression = _T("IT8BL");
		break;
	case COMPRESSION_PIXARFILM:
		szCompression = _T("PIXARFILM");
		break;
	case COMPRESSION_PIXARLOG:
		szCompression = _T("PIXARLOG");
		break;
	case COMPRESSION_DEFLATE:
		szCompression = _T("DEFLATE");
		break;
	case COMPRESSION_ADOBE_DEFLATE:
		szCompression = _T("ADOBE_DEFLATE");
		break;
	case COMPRESSION_DCS:
		szCompression = _T("DCS");
		break;
	case COMPRESSION_JBIG:
		szCompression = _T("JBIG");
		break;
	case COMPRESSION_SGILOG:
		szCompression = _T("SGILOG");
		break;
	case COMPRESSION_SGILOG24:
		szCompression = _T("SGILOG24");
		break;
	}

	return szCompression;
}

static LPCTSTR GetPhotometricString(int nPhotometric)
{
	LPCTSTR szPhotometric = g_szEmptyString;

	switch (nPhotometric)
	{
	case PHOTOMETRIC_MINISWHITE:
		szPhotometric = _T("MINISWHITE");
		break;
	case PHOTOMETRIC_MINISBLACK:
		szPhotometric = _T("MINISBLACK");
		break;
	case PHOTOMETRIC_RGB:
		szPhotometric = _T("RGB");
		break;
	case PHOTOMETRIC_PALETTE:
		szPhotometric = _T("PALETTE");
		break;
	case PHOTOMETRIC_MASK:
		szPhotometric = _T("MASK");
		break;
	case PHOTOMETRIC_SEPARATED:
		szPhotometric = _T("SEPARATED");
		break;
	case PHOTOMETRIC_YCBCR:
		szPhotometric = _T("YCBCR");
		break;
	case PHOTOMETRIC_CIELAB:
		szPhotometric = _T("CIELAB");
		break;
	case PHOTOMETRIC_LOGL:
		szPhotometric = _T("LOGL");
		break;
	case PHOTOMETRIC_LOGLUV:
		szPhotometric = _T("LOGLUV");
		break;
	}

	return szPhotometric;
}

bool CLoadTiff::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	uint32 nHeight = 0;
	uint32 nWidth = 0;
	uint16 nBitsperSample = 0;
	uint16 nSamplesPerpixel = 0;
	uint32 nRowsPerStrip = 0;
	uint16 nPhotometric = 0;
	uint16 nCompression = 0;

	int nPage = -1;
	bool bFirst = true;


	TIFF* hTif = nullptr;

	try
	{
		InstallTiffHandlers();
		hTif = TIFFClientOpen("MemSource", "r", pStreamIn,
		                      _ReadReadProc, _ReadWriteProc,
		                      _TiffSeekProc, _TiffCloseProc, _TiffSizeProc,
		                      _TiffDummyMapProc, _TiffDummyUnmapProc);

		if (hTif == nullptr)
			return false;

		bFirst = true;
		nPage = 0;


		do
		{
			// COMPRESSION/SAMPLESPERPIXEL/BITSPERSAMPLE/PHOTOMETRIC all have TIFF
			// defaults and are legal to omit; plain TIFFGetField would leave them at 0.
			TIFFGetFieldDefaulted(hTif, TIFFTAG_COMPRESSION, &nCompression);
			TIFFGetField(hTif, TIFFTAG_IMAGEWIDTH, &nWidth);
			TIFFGetField(hTif, TIFFTAG_IMAGELENGTH, &nHeight);
			TIFFGetFieldDefaulted(hTif, TIFFTAG_SAMPLESPERPIXEL, &nSamplesPerpixel);
			TIFFGetFieldDefaulted(hTif, TIFFTAG_BITSPERSAMPLE, &nBitsperSample);
			TIFFGetFieldDefaulted(hTif, TIFFTAG_PHOTOMETRIC, &nPhotometric);

			if (nBitsperSample == 0 || nSamplesPerpixel == 0)
				break;

			// The strip reader below cannot decode a tiled directory.
			const bool bTiled = TIFFIsTiled(hTif) != 0;

			if (bFirst)
			{
				// Information
				CString strInformation;
				strInformation.Format(IDS_TIFF_FMT,
				                      nWidth, nHeight, nSamplesPerpixel * nBitsperSample,
				                      GetCompressionString(nCompression),
				                      GetPhotometricString(nPhotometric));

				// Information
				pImageOut->SetStatistics(strInformation);
				pImageOut->SetLoaderName(GetKey());

				IW::CameraSettings settings;
				settings.OriginalImageSize.cx = nWidth;
				settings.OriginalImageSize.cy = nHeight;
				settings.OriginalBpp = IW::PixelFormat::FromBpp(nSamplesPerpixel * nBitsperSample);
				pImageOut->SetCameraSettings(settings);

				bFirst = false;
			}

			int nLength;
			char* pField;

			IW::MetaData iptc(IW::MetaDataTypes::PROFILE_IPTC);
			IW::MetaData xmp(IW::MetaDataTypes::PROFILE_XMP);

			// get a blob
			if (TIFFGetField(hTif, TIFFTAG_PHOTOSHOP, &nLength, &pField) == 1)
			{
				ReadIptcProfile(pField, nLength, iptc);
			}

			if (TIFFGetField(hTif, TIFFTAG_XMLPACKET, &nLength, &pField) == 1)
			{
				ReadXmpProfile(pField, nLength, xmp);
			}

			pImageOut->AddMetaDataBlob(iptc, xmp);

			if (TIFFGetField(hTif, TIFFTAG_ICCPROFILE, &nLength, &pField) == 1)
			{
				IW::MetaData data(IW::MetaDataTypes::PROFILE_ICC, (LPCBYTE)pField, nLength);
				pImageOut->AddBlob(data);
			}

			IW::PixelFormat pf(IW::PixelFormat::PF24);

			switch (nPhotometric)
			{
			case PHOTOMETRIC_MINISWHITE:
			case PHOTOMETRIC_MINISBLACK:
			case PHOTOMETRIC_PALETTE:
				pf = IW::PixelFormat::FromBpp(nSamplesPerpixel * nBitsperSample);
				break;
			case PHOTOMETRIC_MASK:
			case PHOTOMETRIC_SEPARATED:
				break;
			case PHOTOMETRIC_RGB:
			case PHOTOMETRIC_YCBCR:
			case PHOTOMETRIC_CIELAB:
			case PHOTOMETRIC_ICCLAB:
			case PHOTOMETRIC_ITULAB:
			case PHOTOMETRIC_LOGL:
			case PHOTOMETRIC_LOGLUV:
				pf = IW::PixelFormat::FromBpp(nSamplesPerpixel * nBitsperSample);
				break;
			}

			// create a new DIB
			CRect rc(0, 0, nWidth, nHeight);
			pImageOut->CreatePage(rc, pf, true);

			float fResX = 0.0f, fResY = 0.0f;
			uint16 nResUnit = RESUNIT_NONE;
			TIFFGetField(hTif, TIFFTAG_RESOLUTIONUNIT, &nResUnit);
			const bool bHasRes = TIFFGetField(hTif, TIFFTAG_XRESOLUTION, &fResX) == 1 &&
				TIFFGetField(hTif, TIFFTAG_YRESOLUTION, &fResY) == 1;

			IW::CameraSettings settings;
			settings.OriginalImageSize.cx = nWidth;
			settings.OriginalImageSize.cy = nHeight;
			settings.OriginalBpp = pf;

			if (bHasRes && nResUnit == RESUNIT_INCH)
			{
				settings.XPelsPerMeter = IW::InchToMeter(fResX);
				settings.YPelsPerMeter = IW::InchToMeter(fResY);
			}
			else if (bHasRes && nResUnit == RESUNIT_CENTIMETER)
			{
				settings.XPelsPerMeter = IW::CMToMeter(fResX);
				settings.YPelsPerMeter = IW::CMToMeter(fResY);
			}

			pImageOut->SetCameraSettings(settings);

			// now lpBits points to the bottom line
			// set up the colormap based on nPhotometric	
			COLORREF palette[256];
			ZeroMemory(palette, sizeof(COLORREF) * 256);

			switch (nPhotometric)
			{
			case PHOTOMETRIC_MINISBLACK: // bitmap and greyscale image types
			case PHOTOMETRIC_MINISWHITE:
				// Monochrome image

				if (nBitsperSample == 1)
				{
					if (nPhotometric == PHOTOMETRIC_MINISBLACK)
					{
						palette[0] = RGB(0, 0, 0);
						palette[1] = RGB(255, 255, 255);
					}
					else
					{
						palette[0] = RGB(255, 255, 255);
						palette[1] = RGB(0, 0, 0);
					}
				}
				else
				{
					// need to build the scale for greyscale images

					if (nPhotometric == PHOTOMETRIC_MINISBLACK)
					{
						for (int i = 0; i < 256; i++)
						{
							palette[i] = RGB(i, i, i);
						}
					}
					else
					{
						for (int i = 0; i < 256; i++)
						{
							palette[i] = RGB(255 - i, 255 - i, 255 - i);
						}
					}
				}

				pImageOut->SetPalette(palette);

				break;

			case PHOTOMETRIC_PALETTE: // color map indexed
				uint16* red;
				uint16* green;
				uint16* blue;
				BOOL Palette16Bits;

				red = green = blue = nullptr;

				if (!TIFFGetField(hTif, TIFFTAG_COLORMAP, &red, &green, &blue) ||
					red == nullptr || green == nullptr || blue == nullptr)
				{
					break;
				}

				// Is the palette 16 or 8 pBits ?

				if (CheckColormap(1 << nBitsperSample, red, green, blue) == 16)
				{
					Palette16Bits = TRUE;
				}
				else
				{
					Palette16Bits = FALSE;
				}

				// load the palette in the DIB. BitsPerSample is a file-supplied
				// uint16, so the entry count has to be clamped to the buffer.
				int nEntryCount = IW::Min(1 << nBitsperSample, 256) - 1;

				for (int i = nEntryCount; i >= 0; i--)
				{
					if (Palette16Bits)
					{
						// RGB()'s first argument is the low byte, which is blue here.
						palette[i] = RGB(CVT(blue[i]), CVT(green[i]), CVT(red[i]));
					}
					else
					{
						palette[i] = RGB(blue[i], green[i], red[i]);
					}
				}

				pImageOut->SetPalette(palette);
				break;
			}

			// read the tiff lines and save them in the DIB
			// The page above is created either way, so a tiled directory degrades to a
			// blank page rather than dropping this and every later page.
			if (bTiled)
			{
				pImageOut->Flush();
				nPage += 1;
				continue;
			}

			TIFFGetFieldDefaulted(hTif, TIFFTAG_ROWSPERSTRIP, &nRowsPerStrip);
			if (nRowsPerStrip == static_cast<uint32>(-1))
			{
				TIFFGetField(hTif, TIFFTAG_IMAGELENGTH, &nRowsPerStrip);
			}

			// TIFFStripSize is a signed 64-bit tmsize_t derived from the directory;
			// narrowing it to int lets a crafted file under-size the buffer.
			const tmsize_t stripSize = TIFFStripSize(hTif);
			if (stripSize <= 0 || stripSize > kMaxTiffStripBytes / 4)
				break;

			const size_t nStripSize = static_cast<size_t>(stripSize);

			DWORD line = static_cast<DWORD>(TIFFScanlineSize(hTif));
			DWORD nLineNum = 0;

			// The converters write 3 or 4 bytes a pixel whatever the strip size
			// says, SetBitmap copies the page's DWORD-aligned storage width, and
			// the default branch does that out of the *read* buffer. None of
			// those is bounded by TIFFStripSize, which the directory chooses.
			const size_t nStorageWidth = static_cast<size_t>(IW::CalcStorageWidth(nWidth, pf));
			const size_t nPixelBytes = static_cast<size_t>(nWidth) * 4;
			const size_t nConvertBytes = nPixelBytes > nStorageWidth ? nPixelBytes : nStorageWidth;

			// The strip loop never reads more than the page is tall, so this is
			// bounded by the page even when the directory claims otherwise.
			const size_t nRows = nRowsPerStrip < nHeight ? nRowsPerStrip : nHeight;
			const size_t nRowsBytes = nRows * line;
			const size_t nReadBytes = (nRowsBytes > nStripSize ? nRowsBytes : nStripSize) + nStorageWidth;

			IW::CBuffer<BYTE> buffer(nReadBytes), bufferConvert(nConvertBytes);
			LPBYTE pBuf = buffer;
			LPBYTE pBufConvert = bufferConvert;

			for (uint32 y = 0; y < nHeight; y += nRowsPerStrip)
			{
				uint32 nRow = (y + nRowsPerStrip > nHeight ? nHeight - y : nRowsPerStrip);

				// Check for overflow
				assert(nHeight >= (nLineNum + nRow));


				if (TIFFReadEncodedStrip(hTif, TIFFComputeStrip(hTif, y, 0), pBuf, nRow * line) < 0)
				{
					break;
				}

				for (DWORD l = 0; l < nRow; l++)
				{
					LPBYTE pBufIn = pBuf + (l * line);

					switch (nPhotometric)
					{
					case PHOTOMETRIC_RGB:
						if (nSamplesPerpixel == 4)
						{
							IW::ConvertARGBtoABGR(pBufConvert, pBufIn, nWidth);
						}
						else
						{
							IW::ConvertRGBtoBGR(pBufConvert, pBufIn, nWidth);
						}

						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_YCBCR:
						IW::ConvertYCbCrtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_CIELAB:
						IW::ConvertCIELABtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_ICCLAB:
						IW::ConvertICCLABtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_ITULAB:
						IW::ConvertITULABtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_LOGL:
						IW::ConvertLOGLtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					case PHOTOMETRIC_LOGLUV:
						IW::ConvertLOGLUVtoBGR(pBufConvert, pBufIn, nWidth);
						pImageOut->SetBitmap(nLineNum, pBufConvert);
						break;
					default:
						pImageOut->SetBitmap(nLineNum, pBufIn);
						break;
					}

					nLineNum++;
				}

				if (pStatus->QueryCancel())
					break;
			}

			pImageOut->Flush();

			pBuf = nullptr;
			nPage++;
		}
		while (TIFFReadDirectory(hTif));
	}
	catch (std::exception /*&e*/)
	{
		//pStatus->SetError(e.what());
		//ATLTRACE(_T("Exception in CLoadPsd::Read %s"), e.what());
	}

	if (hTif)
		TIFFClose(hTif);

	return true;
}


bool CLoadTiff::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn,
                      const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	// handle standard nWidth/nHeight/bpp stuff
	m_hTiffOut = nullptr;


	InstallTiffHandlers();

	if (!(m_hTiffOut = TIFFClientOpen("MemSource", "w", pStreamOut,
	                                  _WriteReadProc, _WriteWriteProc, _TiffSeekProc, _TiffCloseProc,
	                                  _TiffSizeProc, _TiffDummyMapProc, _TiffDummyUnmapProc)))
	{
		/*LPVOID lpvMessage;                      // temporary message buffer
		// retrieve a message from the system message table
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER |                                                                                                                                                 
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, GetLastError(),
		App.GetLangId(),
		(LPTSTR) &lpvMessage, 0, NULL );
		// display the message in a message box
		MessageBox( NULL, (LPCSTR)lpvMessage, NULL, 
		MB_ICONEXCLAMATION | MB_OK );
		// release the buffer FormatMessage allocated
		LocalFree( lpvMessage );*/

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
			LPCBYTE pIim = nullptr;
			unsigned nIim = 0;

			if (MetadataIPTC::GetIimStream(data, pIim, nIim))
				WriteIptcProfile(m_hTiffOut, pIim, nIim);
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
