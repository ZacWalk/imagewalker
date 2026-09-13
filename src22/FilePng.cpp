// ImageWalker by Zac Walker
//
// Purpose: PNG implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "FilePng.h"

#ifdef COMPILE_PNG

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadPng::CLoadPng() : m_png_ptr(nullptr), m_info_ptr(nullptr)
{
}

CLoadPng::~CLoadPng()
{
}

bool CLoadPng::IsPng(LPCBYTE pByte)
{
	return (png_check_sig(pByte, 8) != 0);
}

using read_struct = struct
{
	LPCBYTE pByte;
	DWORD nSize;
	DWORD nPos;
};


void CLoadPng::read_data(png_struct* png_ptr, png_byte* data, png_size_t length)
{
	USES_CONVERSION;

	auto pStreamIn = static_cast<IW::IStreamIn*>(png_get_io_ptr(png_ptr));

	DWORD dwRead;
	pStreamIn->Read(data, static_cast<DWORD>(length), &dwRead);

	if (dwRead != length)
		png_error(png_ptr, CT2A(App.LoadString(IDS_FAILEDTODECODEIMAGEFILE)));
}


void CLoadPng::user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
	USES_CONVERSION;
	throw std::exception(error_msg);
}

void CLoadPng::user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
}

void CLoadPng::write_data(png_struct* png_ptr, png_byte* data, png_size_t length)
{
	USES_CONVERSION;
	auto pStreamOut = static_cast<IW::IStreamOut*>(png_get_io_ptr(png_ptr));

	DWORD dwWrite;
	pStreamOut->Write(data, static_cast<DWORD>(length), &dwWrite);

	if (dwWrite != length)
		png_error(png_ptr, CT2A(App.LoadString(IDS_FAILEDTOENCODE)));
}

static char* g_png_xmp_keyword = "XML:com.adobe.xmp";

static IW::MetaData ReadMetadata(png_structp png_ptr, png_infop info_ptr)
{
	IW::MetaData xmp(IW::MetaDataTypes::PROFILE_XMP);
	png_textp text_ptr = nullptr;
	int num_text = 0;

	if (png_get_text(png_ptr, info_ptr, &text_ptr, &num_text) > 0)
	{
		for (int i = 0; i < num_text; i++)
		{
			png_text text = text_ptr[i];
			int length = IW::Max(static_cast<int>(text.text_length), static_cast<int>(text.itxt_length));

			if (strcmp(text.key, g_png_xmp_keyword) == 0 && length > 0)
			{
				xmp.CopyData(text.text, length);
			}
		}
	}

	return xmp;
}

static void WriteMetadata(png_structp png_ptr, png_infop info_ptr, const IW::MetaData& xmp)
{
	int size = xmp.GetDataSize();

	if (size > 0)
	{
		png_text text;

		std::string str((char*)xmp.GetData(), xmp.GetDataSize());

		memset(&text, 0, sizeof(png_text));
		text.compression = 1; // iTXt, none
		text.key = g_png_xmp_keyword; // keyword, 1-79 character description of "text"
		text.text = (char*)str.c_str(); // comment, may be an empty string (ie "")
		text.text_length = str.size(); // length of the text string
		text.itxt_length = str.size(); // length of the itxt string
		text.lang = nullptr; // language code, 0-79 characters or a NULL pointer
		text.lang_key = nullptr; // keyword translated UTF-8 string, 0 or more chars or a NULL pointer

		// set the tag 
		png_set_text(png_ptr, info_ptr, &text, 1);
	}
}

bool CLoadPng::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	bool bRead = false;

	try
	{
		m_png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, user_error_fn, user_warning_fn);

		if (!m_png_ptr)
			throw std::bad_alloc();

		m_info_ptr = png_create_info_struct(m_png_ptr);
		if (!m_info_ptr)
		{
			png_destroy_read_struct(&m_png_ptr,
			                        nullptr, nullptr);

			throw std::bad_alloc();
		}

		IW::CameraSettings settings;
		png_uint_32 cxIn, cyIn;
		int bit_depth, color_type, interlace_type;

		// Invert alpha
		//png_set_invert_alpha(m_png_ptr);

		// set up the input control
		png_set_read_fn(m_png_ptr, pStreamIn, read_data);

		// read the file information 
		png_read_info(m_png_ptr, m_info_ptr);

		// get various values
		png_get_IHDR(m_png_ptr, m_info_ptr, &cxIn, &cyIn, &bit_depth, &color_type, &interlace_type, nullptr, nullptr);

		// 16 bits per pixel, you will never need that!
		// tell libpng to strip 16 bit/color files down to 8 bits/color 
		png_set_strip_16(m_png_ptr);

		// We dont handel 4 bits so lets Make it a least 8 bits!
		//if (bit_depth == 4)
		//{
		//	png_set_packing(m_png_ptr);
		//}

		// Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel 
		if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
			png_set_expand_gray_1_2_4_to_8(m_png_ptr);

		// There is no grayscale surface format, so let libpng widen to RGB.
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(m_png_ptr);

		// flip the RGB pixels to BGR (or RGBA to BGRA)
		png_set_bgr(m_png_ptr);


		// Set interlace handling
		int pass = png_set_interlace_handling(m_png_ptr);

		// Details
		CString statistics;

		// png_struct is opaque from libpng 1.5, so pixel depth is derived.
		const int pixel_depth = png_get_bit_depth(m_png_ptr, m_info_ptr) *
			png_get_channels(m_png_ptr, m_info_ptr);

		if (pass > 1)
		{
			statistics.Format(IDS_PNG_INTERLACED_FMT, cxIn, cyIn, pixel_depth, pass);
		}
		else
		{
			statistics.Format(IDS_PNG_FMT, cxIn, cyIn, pixel_depth);
		}

		pImageOut->SetStatistics(statistics);
		pImageOut->SetLoaderName(GetKey());


		// Update info
		png_read_update_info(m_png_ptr, m_info_ptr);

		// png_read_update_info has already rewritten info_ptr to the post-transform
		// geometry, so png_set_strip_16 is accounted for here. Halving again would
		// under-size every row buffer below.
		int real_pixel_depth = png_get_bit_depth(m_png_ptr, m_info_ptr) *
			png_get_channels(m_png_ptr, m_info_ptr);

		IW::PixelFormat pf = IW::PixelFormat::FromBpp(real_pixel_depth);

		settings.OriginalImageSize.cx = cxIn;
		settings.OriginalImageSize.cy = cyIn;
		settings.OriginalBpp = pf;

		// Create the dib
		CRect rcPage(0, 0, cxIn, cyIn);
		int nStorageWidthIn = IW::CalcStorageWidth(cxIn, pf);

		// Do palette
		if (PNG_COLOR_TYPE_PALETTE == color_type &&
			png_get_valid(m_png_ptr, m_info_ptr, PNG_INFO_PLTE))
		{
			COLORREF paletteOut[256] = {};
			png_colorp palette;
			int num_palette;

			png_get_PLTE(m_png_ptr, m_info_ptr, &palette, &num_palette);

			num_palette = IW::Min(num_palette, 1 << real_pixel_depth);

			for (int i = 0; i < num_palette; i++)
			{
				// Entries not covered by tRNS are fully opaque per the PNG spec.
				paletteOut[i] = RGB(palette[i].blue, palette[i].green, palette[i].red) | 0xFF000000;
			}

			if (png_get_valid(m_png_ptr, m_info_ptr, PNG_INFO_tRNS))
			{
				png_bytep trans;
				int num_trans;
				png_color_16p trans_values;

				png_get_tRNS(m_png_ptr, m_info_ptr, &trans, &num_trans, &trans_values);

				num_trans = IW::Min(num_trans, 1 << real_pixel_depth);

				for (int i = 0; i < num_trans; i++)
				{
					paletteOut[i] = (paletteOut[i] & 0x00ffffff) | (trans[i] << 24);
				}

				// png_set_packing is off, so a 1/2/4-bit palette row stays packed and
				// the page must match its depth.
				pImageOut->CreatePage(rcPage,
				                      pf == IW::PixelFormat::PF8 ? IW::PixelFormat::PF8Alpha : pf,
				                      pass == 1);
			}
			else
			{
				pImageOut->CreatePage(rcPage, pf, pass == 1);
			}

			pImageOut->SetPalette(paletteOut);
		}
		else if (PNG_COLOR_TYPE_RGB_ALPHA == color_type || PNG_COLOR_TYPE_GRAY_ALPHA == color_type)
		{
			pImageOut->CreatePage(rcPage, IW::PixelFormat::PF32Alpha, pass == 1);
		}
		else
		{
			pImageOut->CreatePage(rcPage, pf, pass == 1);
		}


		if (png_get_valid(m_png_ptr, m_info_ptr, PNG_INFO_pHYs))
		{
			png_uint_32 res_x, res_y;

			// We'll overload this var and use 0 to mean no phys data,
			// since if it's not in meters we can't use it anyway
			int res_unit_type = 0;

			png_get_pHYs(m_png_ptr, m_info_ptr, &res_x, &res_y, &res_unit_type);

			if (res_unit_type == 1)
			{
				settings.XPelsPerMeter = res_x;
				settings.YPelsPerMeter = res_y;
			}
		}


		int nLine = 0;
		// The row must hold whichever is larger: what libpng writes, or what
		// SetBitmap copies out of it.
		IW::CBuffer<BYTE> pLine(static_cast<size_t>(
			IW::Max(static_cast<int>(png_get_rowbytes(m_png_ptr, m_info_ptr)), nStorageWidthIn)));

		// Read the Image
		for (int j = 0; j < pass; j++)
		{
			for (unsigned i = 0; i < cyIn; i++)
			{
				png_read_row(m_png_ptr, pLine, nullptr);
				pImageOut->SetBitmap(i, pLine);

				if (pStatus->QueryCancel())
				{
					pStatus->SetError(App.LoadString(IDS_CANCELED));
				}

				pStatus->Progress(nLine++, cyIn * pass);
			}
		}

		pImageOut->SetCameraSettings(settings);
		pImageOut->Flush();

		// read the rest of the file, getting any additional chunks in info_ptr
		png_read_end(m_png_ptr, m_info_ptr);

		// XMP
		IW::MetaData iptc(IW::MetaDataTypes::PROFILE_IPTC);
		IW::MetaData xmp = ReadMetadata(m_png_ptr, m_info_ptr);
		pImageOut->AddMetaDataBlob(iptc, xmp);

		if (pImageOut->WantRawImage())
		{
			pImageOut->AddImageData(pStreamIn, IW::MetaDataTypes::PNG_IMAGE);
		}

		bRead = true;
	}
	catch (std::exception& e)
	{
		// user_error_fn throws for an ordinary truncated or corrupt file, so
		// reporting success here would hand the caller a half-decoded page with
		// no raw blob -- which a later rotate re-encodes over the original.
		pStatus->SetError(CString(e.what()));
	}

	// clean up after the read, and free any memory allocated
	png_destroy_read_struct(&m_png_ptr, &m_info_ptr, nullptr);


	return bRead;
}

#define HANDLE_CHUNK_IF_SAFE      2
#define HANDLE_CHUNK_ALWAYS       3


bool CLoadPng::Write(IW::IStreamOut* pStreamOut, IW::IStreamIn* pStreamIn, const IW::Image& imageIn,
                     const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	// user_error_fn throws rather than calling longjmp, so an error here leaves
	// libpng's C frames as a C++ exception.
	try
	{
		return WriteTranscode(pStreamOut, pStreamIn, imageIn, settings, pStatus);
	}
	catch (std::exception& e)
	{
		pStatus->SetError(CString(e.what()));
	}

	return false;
}

bool CLoadPng::WriteTranscode(IW::IStreamOut* pStreamOut, IW::IStreamIn* pStreamIn, const IW::Image& imageIn,
                              const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	png_infop end_info_ptr = nullptr;
	png_structp write_ptr = nullptr;
	png_infop write_info_ptr = nullptr;
	png_infop write_end_info_ptr = nullptr;
	png_bytep row_buf = nullptr;
	png_uint_32 y;
	png_uint_32 width, height;
	int num_pass, pass;
	int bit_depth, color_type;

	m_png_ptr = png_create_read_struct
	(PNG_LIBPNG_VER_STRING, nullptr,
	 user_error_fn, user_warning_fn);

	write_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
	                                    nullptr, user_error_fn, user_warning_fn);

	// The only cleanup that survives a throw out of libpng.
	struct Guard
	{
		png_structp& read_ptr;
		png_infop& read_info;
		png_infop& read_end;
		png_structp& write_ptr;
		png_infop& write_info;
		png_infop& write_end;

		~Guard()
		{
			if (read_ptr) png_destroy_read_struct(&read_ptr, &read_info, &read_end);

			if (write_ptr)
			{
				png_destroy_info_struct(write_ptr, &write_end);
				png_destroy_write_struct(&write_ptr, &write_info);
			}
		}
	} guard{m_png_ptr, m_info_ptr, end_info_ptr, write_ptr, write_info_ptr, write_end_info_ptr};

	if (!m_png_ptr || !write_ptr)
	{
		return false;
	}

	m_info_ptr = png_create_info_struct(m_png_ptr);
	end_info_ptr = png_create_info_struct(m_png_ptr);

	write_info_ptr = png_create_info_struct(write_ptr);
	write_end_info_ptr = png_create_info_struct(write_ptr);

	// set up the input control
	png_set_read_fn(m_png_ptr, pStreamIn, read_data);
	png_set_write_fn(write_ptr, pStreamOut, write_data, nullptr);

	png_set_write_status_fn(write_ptr, nullptr);
	png_set_read_status_fn(m_png_ptr, nullptr);


	png_set_keep_unknown_chunks(m_png_ptr, HANDLE_CHUNK_ALWAYS, nullptr, 0);
	png_set_keep_unknown_chunks(write_ptr, HANDLE_CHUNK_IF_SAFE, nullptr, 0);
	png_read_info(m_png_ptr, m_info_ptr);

	{
		int interlace_type, compression_type, filter_type;

		if (png_get_IHDR(m_png_ptr, m_info_ptr, &width, &height, &bit_depth,
		                 &color_type, &interlace_type, &compression_type, &filter_type))
		{
			png_set_IHDR(write_ptr, write_info_ptr, width, height, bit_depth,
			             color_type, interlace_type, compression_type, filter_type);
		}
	}

	{
		double white_x, white_y, red_x, red_y, green_x, green_y, blue_x,
		       blue_y;
		if (png_get_cHRM(m_png_ptr, m_info_ptr, &white_x, &white_y, &red_x,
		                 &red_y, &green_x, &green_y, &blue_x, &blue_y))
		{
			png_set_cHRM(write_ptr, write_info_ptr, white_x, white_y, red_x,
			             red_y, green_x, green_y, blue_x, blue_y);
		}
	}
	{
		double gamma;

		if (png_get_gAMA(m_png_ptr, m_info_ptr, &gamma))
		{
			png_set_gAMA(write_ptr, write_info_ptr, gamma);
		}
	}

	// Save XMP
	if (!imageIn.IsEmpty())
	{
		WriteMetadata(write_ptr, write_info_ptr, imageIn.GetMetaData(IW::MetaDataTypes::PROFILE_XMP));
	}

	{
		png_charp name;
		png_bytep profile;
		png_uint_32 proflen;
		int compression_type;

		if (png_get_iCCP(m_png_ptr, m_info_ptr, &name, &compression_type, &profile, &proflen))
		{
			png_set_iCCP(write_ptr, write_info_ptr, name, compression_type, profile, proflen);
		}
	}


	{
		int intent;

		if (png_get_sRGB(m_png_ptr, m_info_ptr, &intent))
		{
			png_set_sRGB(write_ptr, write_info_ptr, intent);
		}
	}

	{
		png_colorp palette;
		int num_palette;

		if (png_get_PLTE(m_png_ptr, m_info_ptr, &palette, &num_palette))
		{
			png_set_PLTE(write_ptr, write_info_ptr, palette, num_palette);
		}
	}

	{
		png_color_16p background;

		if (png_get_bKGD(m_png_ptr, m_info_ptr, &background))
		{
			png_set_bKGD(write_ptr, write_info_ptr, background);
		}
	}

	{
		png_uint_16p hist;

		if (png_get_hIST(m_png_ptr, m_info_ptr, &hist))
		{
			png_set_hIST(write_ptr, write_info_ptr, hist);
		}
	}

	{
		png_int_32 offset_x, offset_y;
		int unit_type;

		if (png_get_oFFs(m_png_ptr, m_info_ptr, &offset_x, &offset_y, &unit_type))
		{
			png_set_oFFs(write_ptr, write_info_ptr, offset_x, offset_y, unit_type);
		}
	}

	{
		png_charp purpose, units;
		png_charpp params;
		png_int_32 X0, X1;
		int type, nparams;

		if (png_get_pCAL(m_png_ptr, m_info_ptr, &purpose, &X0, &X1, &type,
		                 &nparams, &units, &params))
		{
			png_set_pCAL(write_ptr, write_info_ptr, purpose, X0, X1, type,
			             nparams, units, params);
		}
	}

	{
		png_uint_32 res_x, res_y;
		int unit_type;

		if (png_get_pHYs(m_png_ptr, m_info_ptr, &res_x, &res_y, &unit_type))
		{
			png_set_pHYs(write_ptr, write_info_ptr, res_x, res_y, unit_type);
		}
	}

	{
		png_color_8p sig_bit;

		if (png_get_sBIT(m_png_ptr, m_info_ptr, &sig_bit))
		{
			png_set_sBIT(write_ptr, write_info_ptr, sig_bit);
		}
	}

	{
		int unit;
		double scal_width, scal_height;

		if (png_get_sCAL(m_png_ptr, m_info_ptr, &unit, &scal_width,
		                 &scal_height))
		{
			png_set_sCAL(write_ptr, write_info_ptr, unit, scal_width, scal_height);
		}
	}


	{
		png_bytep trans;
		int num_trans;
		png_color_16p trans_values;

		if (png_get_tRNS(m_png_ptr, m_info_ptr, &trans, &num_trans,
		                 &trans_values))
		{
			png_set_tRNS(write_ptr, write_info_ptr, trans, num_trans,
			             trans_values);
		}
	}

	{
		png_unknown_chunkp unknowns;
		int num_unknowns = png_get_unknown_chunks(m_png_ptr, m_info_ptr,
		                                          &unknowns);
		if (num_unknowns)
		{
			png_size_t i;
			png_set_unknown_chunks(write_ptr, write_info_ptr, unknowns,
			                       num_unknowns);
			/* copy the locations from the m_info_ptr.  The automatically
			generated locations in write_info_ptr are wrong because we
		haven't written anything yet */
			for (i = 0; i < static_cast<png_size_t>(num_unknowns); i++)
				png_set_unknown_chunk_location(write_ptr, write_info_ptr, static_cast<int>(i),
				                               unknowns[i].location);
		}
	}


	/* If we wanted, we could write info in two steps:
	png_write_info_before_PLTE(write_ptr, write_info_ptr);
	*/
	png_write_info(write_ptr, write_info_ptr);


	num_pass = png_set_interlace_handling(m_png_ptr);
	png_set_interlace_handling(write_ptr);

	IW::CBuffer<BYTE> rowStorage(png_get_rowbytes(m_png_ptr, m_info_ptr));
	row_buf = rowStorage;

	for (pass = 0; pass < num_pass; pass++)
	{
		for (y = 0; y < height; y++)
		{
			png_read_rows(m_png_ptr, &row_buf, nullptr, 1);

			png_write_rows(write_ptr, &row_buf, 1);
		}
	}

	png_free_data(m_png_ptr, m_info_ptr, PNG_FREE_UNKN, -1);
	png_free_data(write_ptr, write_info_ptr, PNG_FREE_UNKN, -1);

	png_read_end(m_png_ptr, end_info_ptr);

	/*
	Direct copy text?
	{
		png_textp text_ptr;
		int num_text;
		
		if (png_get_text(m_png_ptr, end_info_ptr, &text_ptr, &num_text) > 0)
		{
			png_set_text(write_ptr, write_end_info_ptr, text_ptr, num_text);
		}
	}*/


	{
		png_unknown_chunkp unknowns;
		int num_unknowns;
		num_unknowns = png_get_unknown_chunks(m_png_ptr, end_info_ptr,
		                                      &unknowns);
		if (num_unknowns)
		{
			png_size_t i;
			png_set_unknown_chunks(write_ptr, write_end_info_ptr, unknowns,
			                       num_unknowns);
			/* copy the locations from the m_info_ptr.  The automatically
			generated locations in write_end_info_ptr are wrong because we
		haven't written the end_info yet */
			for (i = 0; i < static_cast<png_size_t>(num_unknowns); i++)
				png_set_unknown_chunk_location(write_ptr, write_end_info_ptr, static_cast<int>(i),
				                               unknowns[i].location);
		}
	}

	png_write_end(write_ptr, write_end_info_ptr);

	return true;
}

bool CLoadPng::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	if (imageIn.HasMetaData(IW::MetaDataTypes::PNG_IMAGE))
	{
		const IW::MetaData& data = imageIn.GetMetaData(IW::MetaDataTypes::PNG_IMAGE);
		IW::StreamConstBlob streamIn(data);
			return Write(pStreamOut, &streamIn, imageIn, settings, pStatus);
	}

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
	                                              nullptr, user_error_fn, user_warning_fn);

	if (png_ptr == nullptr)
	{
		return FALSE;
	}

	// Allocate/initialize the image information data.  REQUIRED
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr)
	{
		png_destroy_write_struct(&png_ptr, nullptr);
		return FALSE;
	}

	// Palette holders!

	png_color palette[256];
	png_byte trans[256];

	// Setup write!
	png_set_write_fn(png_ptr, pStreamOut, write_data, nullptr);

	const IW::Page pageIn = imageIn.GetFirstPage();
	IW::ConstIImageSurfaceLockPtr pLockIn = pageIn.GetSurfaceLock();
	IW::PixelFormat pf = pageIn.GetPixelFormat();
	int nColors = pf.NumberOfPaletteEntries();

	png_uint_32 res_x = imageIn.GetXPelsPerMeter();
	png_uint_32 res_y = imageIn.GetYPelsPerMeter();

	if ((res_x > 0) && (res_y > 0))
	{
		png_set_pHYs(png_ptr, info_ptr, res_x, res_y, 1);
	}

	unsigned nColorType = 0;
	int nBpp = 8;

	if (pf.HasPalette())
	{
		nColorType = PNG_COLOR_TYPE_PALETTE;
		nBpp = pf.ToBpp();
	}
	else if (pf == IW::PixelFormat::PF32Alpha)
	{
		nColorType = PNG_COLOR_TYPE_RGB_ALPHA;
	}
	else
	{
		nColorType = PNG_COLOR_TYPE_RGB;
	}


	// set the file information here 
	png_set_IHDR(png_ptr, info_ptr, pageIn.GetWidth(), pageIn.GetHeight(),
	             nBpp, nColorType,
	             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	// set the palette if there is one
	if (nColorType == PNG_COLOR_TYPE_PALETTE)
	{
		IW::LPCCOLORREF pRgb = pageIn.GetPalette();

		for (int i = 0; i < nColors; i++)
		{
			palette[i].red = IW::GetB(pRgb[i]);
			palette[i].green = IW::GetG(pRgb[i]);
			palette[i].blue = IW::GetR(pRgb[i]);
		}

		// tRNS is alpha directly, the same way the reader takes it: 255 opaque.
		// This used to store 255 minus the palette alpha, so an ordinary opaque
		// palette was written out fully transparent. Only a format that carries
		// alpha has anything meaningful in that byte, so only it gets a tRNS.
		if (pf.HasAlpha())
		{
			for (int i = 0; i < nColors; i++)
			{
				trans[i] = static_cast<BYTE>(pRgb[i] >> 24);
			}

			png_set_tRNS(png_ptr, info_ptr, trans, nColors, nullptr);
		}

		png_set_PLTE(png_ptr, info_ptr, palette, nColors);
	}


	// write the file information 
	png_write_info(png_ptr, info_ptr);


	// Invert alpha
	//png_set_invert_alpha(png_ptr);

	if (pf == IW::PixelFormat::PF32Alpha)
	{
		int nWidth = pageIn.GetWidth();
		IW::CBuffer<COLORREF> pLine(nWidth);

		// Write the lines
		for (int y = 0; y < pageIn.GetHeight(); y++)
		{
			// GetLine, not RenderLine: RenderLine composites the pixel over the page
			// background and returns alpha 0, which would store a transparent file.
			pLockIn->GetLine(pLine, y, 0, nWidth);

			for (int x = 0; x < nWidth; x++)
			{
				pLine[x] = IW::SwapRB(pLine[x]);
			}

			png_write_row(png_ptr, reinterpret_cast<png_bytep>(pLine.data()));

			if (pStatus->QueryCancel())
			{
				pStatus->SetError(App.LoadString(IDS_CANCELED));
			}

			pStatus->Progress(y, pageIn.GetHeight());
		}
	}
	else if (pf.HasPalette() || pf._pf == IW::PixelFormat::PF24)
	{
		if (nColorType != PNG_COLOR_TYPE_PALETTE)
		{
			// flip bgr pixels to rgb
			png_set_bgr(png_ptr);
		}

		// Write the lines
		for (int i = 0; i < pageIn.GetHeight(); i++)
		{
			png_write_row(png_ptr, pageIn.GetBitmapLine(i));

			if (pStatus->QueryCancel())
			{
				pStatus->SetError(App.LoadString(IDS_CANCELED));
			}

			pStatus->Progress(i, pageIn.GetHeight());
		}
	}
	else
	{
		int nWidth = pageIn.GetWidth();
		IW::CBuffer<COLORREF> pLine(nWidth);

		// Write the lines
		for (int y = 0; y < pageIn.GetHeight(); y++)
		{
			pLockIn->RenderLine(pLine, y, 0, nWidth);
			auto pOut = reinterpret_cast<LPBYTE>(pLine.data());

			for (int x = 0; x < nWidth; x++)
			{
				DWORD c = pLine[x];

				*pOut++ = IW::GetB(c);
				*pOut++ = IW::GetG(c);
				*pOut++ = IW::GetR(c);
			}

			png_write_row(png_ptr, reinterpret_cast<png_bytep>(pLine.data()));

			if (pStatus->QueryCancel())
			{
				pStatus->SetError(App.LoadString(IDS_CANCELED));
			}

			pStatus->Progress(y, pageIn.GetHeight());
		}
	}

	// Save XMP
	if (!imageIn.IsEmpty())
	{
		WriteMetadata(png_ptr, info_ptr, imageIn.GetMetaData(IW::MetaDataTypes::PROFILE_XMP));
	}

	// write the rest of the file
	png_write_end(png_ptr, info_ptr);

	// clean up after the write, and free any memory allocated
	png_destroy_write_struct(&png_ptr, &info_ptr);

	return TRUE;
}

#endif // COMPILE_PNG
