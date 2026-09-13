// LoadJpg.cpp: implementation of the CLoadJpg class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "LoadJpg.h"
#include "dibthumb.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// Global JPEG error

#include <setjmp.h>
#include <io.h>

extern "C" {
/*#include "jpeg6a\jpeglib.h"*/

/* Expanded data source object for stdio input */


METHODDEF(void) init_source(j_decompress_ptr cinfo)
{
}

static const JOCTET buffer[] = {0xFF, JPEG_EOI};

METHODDEF(boolean) fill_input_buffer(j_decompress_ptr cinfo)
{
	cinfo->src->bytes_in_buffer = 2;
	cinfo->src->next_input_byte = buffer;

	return TRUE;
}


METHODDEF(void) skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	/* Just a dumb implementation for now.  Could use fseek() except
	 * it doesn't work on pipes.  Not clear that being smart is worth
	 * any trouble anyway --- large skips are infrequent.
	 */
	if (num_bytes > 0)
	{
		while (num_bytes > static_cast<long>(cinfo->src->bytes_in_buffer))
		{
			num_bytes -= static_cast<long>(cinfo->src->bytes_in_buffer);
			(void)fill_input_buffer(cinfo);
			/* note we assume that fill_input_buffer will never return FALSE,
			 * so suspension need not be handled.
			 */
		}
		cinfo->src->next_input_byte += static_cast<size_t>(num_bytes);
		cinfo->src->bytes_in_buffer -= static_cast<size_t>(num_bytes);
	}
}


/*
 * An additional method that can be provided by data source modules is the
 * resync_to_restart method for error recovery in the presence of RST markers.
 * For the moment, this source module just uses the default resync method
 * provided by the JPEG library.  That method assumes that no backtracking
 * is possible.
 */


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

METHODDEF(void)
term_source(j_decompress_ptr cinfo)
{
	/* no work necessary here */
}
}

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void) ima_jpeg_error_exit(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];

	// Create the message 
	(*cinfo->err->format_message)(cinfo, buffer);

	// Send it to stderr, adding a newline
	TRACE("%s\n", buffer);

	auto pErr = reinterpret_cast<ima_error_mgr*>(cinfo->err);

	// Unwind to the setjmp in CLoadJpg::Load. Throwing from here would cross
	// libjpeg's own C frames, which carry no unwind information.
	if (pErr->bCanJump)
		longjmp(pErr->jmpbuf, 1);

	IW::Throw("bad jpeg file");
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadJpg::CLoadJpg()
{
	/* We set up the normal JPEG error routines, then override error_exit. */
	jerr.bCanJump = FALSE;
	in.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = ima_jpeg_error_exit;
	in.client_data = nullptr;

	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress(&in);


	/* Step 2: specify data source (eg, a file) */
	in.src = &src;

	src.init_source = init_source;
	src.fill_input_buffer = fill_input_buffer;
	src.skip_input_data = skip_input_data;
	src.resync_to_restart = jpeg_resync_to_restart; /* use default method */
	src.term_source = term_source;
}

CLoadJpg::~CLoadJpg()
{
	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress(&in);
}

template <class Iterator>
class CIteratorJpg : public Iterator
{
public:
	CIteratorJpg(j_decompress_ptr pIn)
	{
		m_pIn = pIn;
		m_pLineIn = new BYTE[pIn->output_width * pIn->output_components];
	}

	~CIteratorJpg()
	{
		delete[] m_pLineIn;
	}

	LPBYTE m_pLineIn;
	j_decompress_ptr m_pIn;

	UINT Height() { return m_pIn->output_height; };
	UINT Width() { return m_pIn->output_width; };

	LPBYTE ScanLine()
	{
		jpeg_read_scanlines(m_pIn, &m_pLineIn, 1);

		return m_pLineIn;
	}
};


BOOL CLoadJpg::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	// Nothing with a destructor may live in this frame: the longjmp out of
	// ima_jpeg_error_exit lands here.
	jerr.bCanJump = TRUE;

#pragma warning(suppress: 4611) // setjmp with C++ destruction - see above
	if (setjmp(jerr.jmpbuf) != 0)
	{
		jerr.bCanJump = FALSE;
		jpeg_abort((j_common_ptr)&in);
		return pDib->IsOpen();
	}

	const BOOL b = LoadImage(pDib, pByte, nSize, pStatus, bThumb);
	jerr.bCanJump = FALSE;

	return b;
}

BOOL CLoadJpg::LoadImage(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	try
		{
			/* Step 2: specify data source (eg, a file) */
			src.bytes_in_buffer = nSize;
			src.next_input_byte = pByte;

			/* Step 3: read file parameters with jpeg_read_header() */
			jpeg_read_header(&in, TRUE);

			/* set parameters for decompression */

			if (bThumb)
				in.scale_denom = max(1u, max(in.image_width / IMAGE_X, in.image_height / IMAGE_Y));

			if (in.scale_denom)
			{
				in.dither_mode = JDITHER_NONE;
				in.dct_method = JDCT_FASTEST;
				in.do_fancy_upsampling = FALSE;
				in.two_pass_quantize = FALSE;
			}
			else //Slow but good qual
			{
				//in.dct_method = JDCT_FLOAT;
				//in.do_fancy_upsampling = TRUE;
				in.dither_mode = JDITHER_NONE;
				in.dct_method = JDCT_FASTEST;
				in.do_fancy_upsampling = FALSE;
				in.two_pass_quantize = FALSE;
			}

			jpeg_start_decompress(&in);

			UINT nSrcHeight = in.output_height;
			UINT nSrcWidth = in.output_width;
			UINT nBpp = in.output_components * 8;

			pDib->m_strInfo.Format("%dx%dx%d JPG",
			                       nSrcWidth, nSrcHeight, nBpp);

			ASSERT(nBpp == 8 || nBpp == 24);

			if (!bThumb ||
				(IMAGE_X >= nSrcWidth && IMAGE_Y >= nSrcHeight))
			{
				if (pDib->Create(nSrcWidth, nSrcHeight, nBpp, FALSE))
				{
					int i = 0;

					while (in.output_scanline < in.output_height)
					{
						BYTE* p[4];

						p[0] = pDib->GetBitmap(i);
						p[1] = pDib->GetBitmap((i + 1) % in.output_height);
						p[2] = pDib->GetBitmap((i + 2) % in.output_height);
						p[3] = pDib->GetBitmap((i + 3) % in.output_height);

						i += jpeg_read_scanlines(&in, p, 4);

						if (pStatus && pStatus->Status(in.output_scanline, in.output_height))
								IW::Throw("cancelled");
					}
				}
			}
			else
			{
				if (nBpp == 24)
				{
					CIteratorJpg<CDibIterator24> jpg(&in);
					CDibThumb<CIteratorJpg<CDibIterator24>> Scale;
					Scale.Scale(*pDib, jpg);
				}
				else
				{
					CIteratorJpg<CDibIterator8np> jpg(&in);
					CDibThumb<CIteratorJpg<CDibIterator8np>> Scale;
					Scale.Scale(*pDib, jpg);
				}
			}

			jpeg_finish_decompress(&in);

			if (nBpp == 24)
				SwapRB(pDib);
		}
	catch (...)
		{
			jpeg_abort((j_common_ptr)&in);
		}


	return pDib->IsOpen();
}
