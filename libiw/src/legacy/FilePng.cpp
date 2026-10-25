// ImageWalker by Zac Walker
//
// Purpose: PNG implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "FilePng.h"
#include "LibfilesDocument.h"

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
		png_error(png_ptr, CT2A(CString(IW::Text::FAILEDTODECODEIMAGEFILE)));
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
		png_error(png_ptr, CT2A(CString(IW::Text::FAILEDTOENCODE)));
}

static char* g_png_xmp_keyword = "XML:com.adobe.xmp";

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
	return iw::imaging::detail::read_libfiles_document(libfiles::image_format::png,
		GetKey(), pStreamIn, pImageOut, pStatus);
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
	str.Format(IW::Text::ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
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
	str.Format(IW::Text::ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
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
				pStatus->SetError(CString(IW::Text::CANCELED));
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
				pStatus->SetError(CString(IW::Text::CANCELED));
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
				pStatus->SetError(CString(IW::Text::CANCELED));
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
