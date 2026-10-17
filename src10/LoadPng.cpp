// LoadPng.cpp: implementation of the CLoadPng class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "LoadPng.h"
#include "dibthumb.h"


#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadPng::CLoadPng()
{
}

CLoadPng::~CLoadPng()
{
}

BOOL CLoadPng::IsPng(LPBYTE pByte)
{
	return (png_check_sig(pByte, 8));
}

using read_struct = struct
{
	LPBYTE pByte;
	DWORD nSize;
	DWORD nPos;
};


void CLoadPng::read_data(png_struct* png_ptr, png_byte* data, png_size_t length)
{
	auto p = static_cast<read_struct*>(png_get_io_ptr(png_ptr));

	if (p->nPos + length > p->nSize)
		png_error(png_ptr, "Read Error");

	CopyMemory(data, p->pByte + p->nPos, length);

	p->nPos += static_cast<DWORD>(length);
}

void CLoadPng::user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
	TRACE("%s\n", error_msg);
	IW::Throw("bad png file");
}

void CLoadPng::user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
	TRACE("%s\n", warning_msg);
}

template <class Iterator>
class CIteratorPng : public Iterator
{
public:
	CIteratorPng(png_structp png_ptr, png_infop info_ptr, RGBQUAD* pRgb)
	{
		m_info_ptr = info_ptr;
		m_png_ptr = png_ptr;
		m_pRgb = (COLORREF*)pRgb;
		m_nRowNumber = 0;

		int bit_depth, color_type, interlace_type;

		png_get_IHDR(m_png_ptr, m_info_ptr, &m_nWidth,
		             &m_nHeight,
		             &bit_depth, &color_type, &interlace_type, nullptr, nullptr);


		int real_pixel_depth = max(8, png_get_bit_depth(png_ptr, info_ptr) * png_get_channels(png_ptr, info_ptr));
		if (bit_depth == 16)
			real_pixel_depth /= 2;

		m_pLineIn = new BYTE[Width() * real_pixel_depth];
	}

	~CIteratorPng()
	{
		delete[] m_pLineIn;
	}

	LPBYTE m_pLineIn;

	png_infop m_info_ptr;
	png_structp m_png_ptr;

	UINT m_nWidth;
	UINT m_nHeight;
	UINT m_nRowNumber;

	UINT Height() { return m_nHeight; };
	UINT Width() { return m_nWidth; };

	// libpng 1.6 hides row_number, and png_read_row longjmps on error rather
	// than failing to advance, so the row counter is ours now.
	LPBYTE ScanLine()
	{
		if (m_nRowNumber >= m_nHeight)
			return nullptr;

		png_read_row(m_png_ptr, m_pLineIn, nullptr);
		m_nRowNumber++;

		return m_pLineIn;
	}
};


BOOL CLoadPng::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	read_struct r;

	r.pByte = pByte;
	r.nSize = nSize;
	r.nPos = 0;

	try
		{
			png_ptr = png_create_read_struct
			(PNG_LIBPNG_VER_STRING, nullptr,
			 user_error_fn, user_warning_fn);

			if (!png_ptr)
				IW::ThrowOutOfMemory();

			info_ptr = png_create_info_struct(png_ptr);
			if (!info_ptr)
			{
				png_destroy_read_struct(&png_ptr,
				                        nullptr, nullptr);

				IW::ThrowOutOfMemory();
			}

			png_uint_32 nSrcWidth, nSrcHeight;
			int bit_depth, color_type, interlace_type;

			// Invert alpha
			//png_set_invert_alpha(png_ptr);

			// set up the input control
			png_set_read_fn(png_ptr, &r, read_data);

			// read the file information 
			png_read_info(png_ptr, info_ptr);

			// get various values
			png_get_IHDR(png_ptr, info_ptr, &nSrcWidth, &nSrcHeight, &bit_depth, &color_type, &interlace_type, nullptr,
			             nullptr);

			// 16 bits per pixel, you will never need that!
			// tell libpng to strip 16 bit/color files down to 8 bits/color 
			png_set_strip_16(png_ptr);

			// Lets Make it a least 8 bits!
			png_set_packing(png_ptr);

			// Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel 
			if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
				png_set_expand(png_ptr);

			// flip the RGB pixels to BGR (or RGBA to BGRA)
			png_set_bgr(png_ptr);


			// Set interlace handling
			unsigned pass = png_set_interlace_handling(png_ptr);

			// Details
			pDib->m_strInfo.Format("%dx%dx%d (%d) PNG", nSrcWidth, nSrcHeight,
			                       png_get_bit_depth(png_ptr, info_ptr) * png_get_channels(png_ptr, info_ptr), pass);

			// Update info
			png_read_update_info(png_ptr, info_ptr);

			int real_pixel_depth = max(8, png_get_bit_depth(png_ptr, info_ptr) * png_get_channels(png_ptr, info_ptr));

			if ((!bThumb) ||
				(IMAGE_X >= nSrcWidth && IMAGE_Y >= nSrcHeight))
			{
				if (!Load(pDib, pass))
				{
					png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
					return FALSE;
				}
			}
			else
			{
				// Only filled for PNG_COLOR_TYPE_PALETTE; greyscale depths index it too.
				RGBQUAD pRgb[256] = {};

				// Do palette
				if (PNG_COLOR_TYPE_PALETTE == color_type &&
					png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE))
				{
					png_colorp palette;
					int num_palette;

					png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

					for (int i = 0; i < num_palette; i++)
					{
						pRgb[i].rgbRed = palette[i].red;
						pRgb[i].rgbGreen = palette[i].green;
						pRgb[i].rgbBlue = palette[i].blue;
						pRgb[i].rgbReserved = 0xff;
					}

					if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
					{
						png_bytep trans;
						int num_trans;
						png_color_16p trans_values;

						png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);

						for (int i = 0; i < num_trans; i++)
						{
							// tRNS is alpha directly: 255 opaque, 0 transparent.
							pRgb[i].rgbReserved = trans[i];
						}
					}
				}

				if (pass == 1)
				{
					switch (real_pixel_depth)
					{
					case 8:
						{
							if (PNG_COLOR_TYPE_PALETTE == color_type)
							{
								// Not interlaced
								CIteratorPng<CDibIterator8> png(png_ptr, info_ptr, pRgb);
								CDibThumb<CIteratorPng<CDibIterator8>> Scale;
								Scale.Scale(*pDib, png);
							}
							else
							{
								CIteratorPng<CDibIterator8np> png(png_ptr, info_ptr, pRgb);
								CDibThumb<CIteratorPng<CDibIterator8np>> Scale;
								Scale.Scale(*pDib, png);
							}
							break;
						}
					case 24:
						{
							// Not interlaced
							CIteratorPng<CDibIterator24> png(png_ptr, info_ptr, pRgb);
							CDibThumb<CIteratorPng<CDibIterator24>> Scale;
							Scale.Scale(*pDib, png);
							break;
						}
					case 32:
						{
							// Not interlaced
							CIteratorPng<CDibIterator32> png(png_ptr, info_ptr, pRgb);
							CDibThumb<CIteratorPng<CDibIterator32>> Scale;
							Scale.Scale(*pDib, png);
							break;
						}
					}
				}
				else
				{
					CDib dib;

					Load(&dib, pass);

					switch (real_pixel_depth)
					{
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
					}
				}
			}

			// read the rest of the file, getting any additional chunks in info_ptr
			png_read_end(png_ptr, info_ptr);

			png_textp text_ptr;
			int num_text;

			if (png_get_text(png_ptr, info_ptr, &text_ptr, &num_text) > 0)
			{
				// Comments
				for (int i = 0; i < num_text; i++)
				{
					pDib->m_strInfo += "\n";
					pDib->m_strInfo += text_ptr[i].key;
					pDib->m_strInfo += ": ";
					pDib->m_strInfo += text_ptr[i].text;
				}
			}
		}
	catch (...)
		{
			png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
			return FALSE;
		}

	// clean up after the read, and free any memory allocated
	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

	//png_destroy_info_struct(&info_ptr, (png_infopp)NULL);


	return TRUE;
}

BOOL CLoadPng::Load(CDib* pDib, UINT pass)
{
	png_uint_32 nSrcWidth, nSrcHeight;
	int bit_depth, color_type, interlace_type;

	// get various values
	png_get_IHDR(png_ptr, info_ptr, &nSrcWidth, &nSrcHeight,
	             &bit_depth, &color_type, &interlace_type, nullptr, nullptr);


	int real_pixel_depth = max(8, png_get_bit_depth(png_ptr, info_ptr) * png_get_channels(png_ptr, info_ptr));
	if (bit_depth == 16)
		real_pixel_depth /= 2;


	// Create the dib
	if (!pDib->Create(nSrcWidth, nSrcHeight, real_pixel_depth, FALSE))
		return FALSE;

	// Do palette
	if (PNG_COLOR_TYPE_PALETTE == color_type &&
		png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE))
	{
		RGBQUAD* pRgb = pDib->GetColor();

		png_colorp palette;
		int num_palette;

		png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

		for (int i = 0; i < num_palette; i++)
		{
			pRgb[i].rgbRed = palette[i].red;
			pRgb[i].rgbGreen = palette[i].green;
			pRgb[i].rgbBlue = palette[i].blue;
		}

		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		{
			png_bytep trans;
			int num_trans;
			png_color_16p trans_values;

			png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);

			for (int i = 0; i < num_trans; i++)
			{
				// tRNS is alpha directly: 255 opaque, 0 transparent.
				pRgb[i].rgbReserved = trans[i];
			}
		}
	}

	// Read the Image
	for (unsigned j = 0; j < pass; j++)
		for (unsigned i = 0; i < nSrcHeight; i++)
			png_read_row(png_ptr, pDib->GetBitmap(i), nullptr);

	return TRUE;
}
