// ImageWalker by Zac Walker
//
// Purpose: JPEG implementation - decode, encode, marker handling and camera
//          settings.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "iw/jfifdensity.h"
#include "ViewModelItems.h"
#include "FileJpeg.h"

// COM    User comments
// APP0   JFIF data (+ thumbnail)
// APP1   Exif or XMP data
// APP1   Maker notes
// APP2   FPXR data or ICC profiles
// APP3   additional Exif-like data
// APP4   HPSC
// APP12  PreExif ASCII meta
// APP13  IPTC and PhotoShop data
// APP14  Adobe tags

#include "FileJpegLib.h"
#include "MetadataExif.h"


METHODDEF(void) iw_error_exit(j_common_ptr cinfo)
{
	USES_CONVERSION;
	char sz[JMSG_LENGTH_MAX] = {0};

	// Create the message 
	(*cinfo->err->format_message)(cinfo, sz);
	throw std::exception(sz);
}

/*
* Actual output of an error or trace message.
* Applications may override this method to send JPEG messages somewhere
* other than stderr.
*
* On Windows, printing to stderr is generally completely useless,
* so we provide optional code to produce an error-dialog popup.
* Most Windows applications will still prefer to override this routine,
* but if they don't, it'll do something at least marginally useful.
*
* NOTE: to use the library in an environment that doesn't support the
* C stdio library, you may have to delete the call to fprintf() entirely,
* not just not use this routine.
*/

METHODDEF(void) iw_output_message(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];

	/* Create the message */
	(*cinfo->err->format_message)(cinfo, buffer);

#ifdef USE_WINDOWS_MESSAGEBOX
	/* Display it in a message dialog box */
	MessageBox(IW::GetMainWindow(), buffer, "JPEG Library Error",
	           MB_OK | MB_ICONERROR);
#else
	/* Send it to stderr, adding a newline */
	fprintf(stderr, "%s\n", buffer);
#endif
}


/*
* Decide whether to emit a trace or warning message.
* msg_level is one of:
*   -1: recoverable corrupt-data warning, may want to abort.
*    0: important advisory messages (always display to user).
*    1: first level of tracing detail.
*    2,3,...: successively more detailed tracing messages.
* An application might override this method if it wanted to abort on warnings
* or change the policy about which messages to display.
*/

METHODDEF(void) iw_emit_message(j_common_ptr cinfo, int msg_level)
{
	struct jpeg_error_mgr* err = cinfo->err;

	if (msg_level < 0)
	{
		/* It's a warning message.  Since corrupt files may generate many warnings,
		* the policy implemented here is to show only the first warning,
		* unless trace_level >= 3.
		*/
		if (err->num_warnings == 0 || err->trace_level >= 3)
			(*err->output_message)(cinfo);
		/* Always count warnings in num_warnings. */
		err->num_warnings++;
	}
	else
	{
		/* It's a trace message.  Show it if trace_level >= msg_level. */
		if (err->trace_level >= msg_level)
			(*err->output_message)(cinfo);
	}
}


METHODDEF(void) iw_format_message(j_common_ptr cinfo, char* buffer)
{
	struct jpeg_error_mgr* err = cinfo->err;
	int msg_code = err->msg_code;
	const char* msgtext = nullptr;
	const char* msgptr;
	char ch;
	boolean isstring;

	// Look up message string in proper table 
	if (msg_code > 0 && msg_code <= err->last_jpeg_message)
	{
		msgtext = err->jpeg_message_table[msg_code];
	}
	else if (err->addon_message_table != nullptr &&
		msg_code >= err->first_addon_message &&
		msg_code <= err->last_addon_message)
	{
		msgtext = err->addon_message_table[msg_code - err->first_addon_message];
	}

	// Defend against bogus message number 
	if (msgtext == nullptr)
	{
		err->msg_parm.i[0] = msg_code;
		msgtext = err->jpeg_message_table[0];
	}

	// Check for string parameter, as indicated by %s in the message text
	isstring = FALSE;
	msgptr = msgtext;
	while ((ch = *msgptr++) != '\0')
	{
		if (ch == '%')
		{
			if (*msgptr == 's') isstring = TRUE;
			break;
		}
	}

	// Format the message into the passed buffer
	if (isstring)
	{
		sprintf_s(buffer, JMSG_LENGTH_MAX, msgtext, err->msg_parm.s);
	}
	else
	{
		sprintf_s(buffer, JMSG_LENGTH_MAX, msgtext,
		          err->msg_parm.i[0], err->msg_parm.i[1],
		          err->msg_parm.i[2], err->msg_parm.i[3],
		          err->msg_parm.i[4], err->msg_parm.i[5],
		          err->msg_parm.i[6], err->msg_parm.i[7]);
	}
}


/*
* Reset error state variables at start of a new image.
* This is called during compression startup to reset trace/error
* processing to default state, without losing any application-specific
* method pointers.  An application might possibly want to override
* this method if it has additional error processing state.
*/

METHODDEF(void) iw_reset_error_mgr(j_common_ptr cinfo)
{
	cinfo->err->num_warnings = 0;
	/* trace_level is not reset since it is an application-supplied parameter */
	cinfo->err->msg_code = 0; /* may be useful as a flag for "no error" */
}


/*
* Fill in the standard error-handling methods in a jpeg_error_mgr object.
* Typical call is:
*	struct jpeg_compress_struct cinfo;
*	struct jpeg_error_mgr err;
*
*	cinfo.err = jpeg_iw_error(&err);
* after which the application may override some of the methods.
*/

GLOBAL(struct jpeg_error_mgr *) jpeg_iw_error(struct jpeg_error_mgr* err)
{
	// libjpeg-turbo exports neither jpeg_std_message_table nor JMSG_LASTMSGCODE,
	// so let it fill in the standard fields before the overrides go on top.
	jpeg_std_error(err);

	err->error_exit = iw_error_exit;
	err->emit_message = iw_emit_message;
	err->output_message = iw_output_message;
	err->format_message = iw_format_message;
	err->reset_error_mgr = iw_reset_error_mgr;

	err->trace_level = 0; /* default = no tracing */
	err->num_warnings = 0; /* no warnings emitted yet */
	err->msg_code = 0; /* may be useful as a flag for "no error" */

	err->addon_message_table = nullptr;
	err->first_addon_message = 0; /* for safety */
	err->last_addon_message = 0;

	return err;
}

//}


// The source and destination managers below read from and write to an
// IW stream rather than a FILE. Based on jdatasrc.c and jdatadst.c from the
// Independent JPEG Group's software, by Thomas G. Lane; the original notice:
//
// Based on work by Thomas G. Lane.


extern "C" {

/* Expanded data destination object for ImageWalker stream output */

using my_destination_mgr = struct
{
	struct jpeg_destination_mgr pub; /* public fields */

	IW::IStreamOut* outfile; /* target stream */
	JOCTET* buffer; /* start of buffer */
};

using my_dest_ptr = my_destination_mgr*;

METHODDEF(void)
init_destination(j_compress_ptr cinfo)
{
	auto dest = (my_dest_ptr)cinfo->dest;

	/* Allocate the output buffer --- it will be released when done with image */
	dest->buffer = static_cast<JOCTET*>((*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE,
	                                                               IW::LoadBufferSize * SIZEOF(JOCTET)));

	dest->pub.next_output_byte = dest->buffer;
	dest->pub.free_in_buffer = IW::LoadBufferSize;
}


METHODDEF(boolean)
empty_output_buffer(j_compress_ptr cinfo)
{
	auto dest = (my_dest_ptr)cinfo->dest;
	DWORD dwWritten = 0;

	if (!dest->outfile->Write(dest->buffer, IW::LoadBufferSize, &dwWritten) ||
		dwWritten != IW::LoadBufferSize)
		ERREXIT(cinfo, JERR_FILE_WRITE);

	dest->pub.next_output_byte = dest->buffer;
	dest->pub.free_in_buffer = IW::LoadBufferSize;

	return TRUE;
}


METHODDEF(void)
term_destination(j_compress_ptr cinfo)
{
	auto dest = (my_dest_ptr)cinfo->dest;
	size_t datacount = IW::LoadBufferSize - dest->pub.free_in_buffer;
	DWORD dwWrite = 0;

	/* Write any data remaining in the buffer */
	if (datacount > 0)
	{
		if (!dest->outfile->Write(dest->buffer, static_cast<DWORD>(datacount), &dwWrite) ||
			dwWrite != datacount)
			ERREXIT(cinfo, JERR_FILE_WRITE);
	}
}


void jpeg_iw_dest(j_compress_ptr cinfo, IW::IStreamOut* outfile)
{
	my_dest_ptr dest;


	if (cinfo->dest == nullptr)
	{
		/* first time for this JPEG object? */
		cinfo->dest = static_cast<struct jpeg_destination_mgr*>((*cinfo->mem->alloc_small)(
			(j_common_ptr)cinfo, JPOOL_PERMANENT,
			SIZEOF(my_destination_mgr)));
	}

	dest = (my_dest_ptr)cinfo->dest;
	dest->pub.init_destination = init_destination;
	dest->pub.empty_output_buffer = empty_output_buffer;
	dest->pub.term_destination = term_destination;
	dest->outfile = outfile;
}



METHODDEF(void)
init_source(j_decompress_ptr cinfo)
{
	auto src = (my_src_ptr)cinfo->src;

	// We reset the empty-input-file flag for each image,
	// but we don't clear the input buffer.
	// This is correct behavior for reading a series of images from one source.
	src->start_of_file = TRUE;
}

METHODDEF(boolean)
fill_input_buffer(j_decompress_ptr cinfo)
{
	auto src = (my_src_ptr)cinfo->src;
	DWORD nbytes = 0;

	src->pStream->Read(src->buffer, IW::LoadBufferSize, &nbytes);

	if (nbytes <= 0)
	{
		if (src->start_of_file) /* Treat empty input file as fatal error */
			ERREXIT(cinfo, JERR_INPUT_EMPTY);
		WARNMS(cinfo, JWRN_JPEG_EOF);
		// Insert a fake EOI marker
		src->buffer[0] = static_cast<JOCTET>(0xFF);
		src->buffer[1] = static_cast<JOCTET>(JPEG_EOI);
		nbytes = 2;
	}

	src->pub.next_input_byte = src->buffer;
	src->pub.bytes_in_buffer = nbytes;
	src->start_of_file = FALSE;

	return TRUE;
}


METHODDEF(void)
skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	auto src = (my_src_ptr)cinfo->src;

	/* Just a dumb implementation for now.  Could use fseek() except
	* it doesn't work on pipes.  Not clear that being smart is worth
	* any trouble anyway --- large skips are infrequent.
	*/
	if (num_bytes > 0)
	{
		while (num_bytes > static_cast<long>(src->pub.bytes_in_buffer))
		{
			num_bytes -= static_cast<long>(src->pub.bytes_in_buffer);
			(void)fill_input_buffer(cinfo);
			/* note we assume that fill_input_buffer will never return FALSE,
			* so suspension need not be handled.
			*/
		}
		src->pub.next_input_byte += static_cast<size_t>(num_bytes);
		src->pub.bytes_in_buffer -= static_cast<size_t>(num_bytes);
	}
}


METHODDEF(void)
term_source(j_decompress_ptr cinfo)
{
	/* no work necessary here */
}


void jpeg_iw_src(j_decompress_ptr cinfo, IW::IStreamIn* pStream, IW::IImageStream* pImageOut)
{
	my_src_ptr src;

	// The source object and input buffer are made permanent so that a series
	// of JPEG images can be read from the same file by calling jpeg_iw_src
	// only before the first one.  (If we discarded the buffer at the end of
	// one image, we'd likely lose the start of the next one.)
	// This makes it unsafe to use this manager and a different source
	// manager serially with the same JPEG object.  Caveat programmer.
	if (cinfo->src == nullptr)
	{
		// first time for this JPEG object?
		cinfo->src = static_cast<struct jpeg_source_mgr*>((*cinfo->mem->alloc_small)(
			(j_common_ptr)cinfo, JPOOL_PERMANENT,
			SIZEOF(my_source_mgr)));
		src = (my_src_ptr)cinfo->src;
		src->buffer = static_cast<JOCTET*>((*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_PERMANENT,
		                                                              IW::LoadBufferSize * SIZEOF(JOCTET)));
	}

	src = (my_src_ptr)cinfo->src;
	src->pub.init_source = init_source;
	src->pub.fill_input_buffer = fill_input_buffer;
	src->pub.skip_input_data = skip_input_data;
	src->pub.resync_to_restart = jpeg_resync_to_restart; // use default method
	src->pub.term_source = term_source;
	src->pStream = pStream;
	src->pImageOut = pImageOut;
	src->pub.bytes_in_buffer = 0; // forces fill_input_buffer on first read
	src->pub.next_input_byte = nullptr; // until buffer loaded
}
}


static bool LoadJpegImage(struct jpeg_decompress_struct* pDecompress, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	UINT cyIn = pDecompress->output_height;
	UINT cxIn = pDecompress->output_width;
	IW::PixelFormat pf = IW::PixelFormat::PF24;

	// Use colour space to determine bpp
	switch (pDecompress->out_color_space)
	{
	case JCS_GRAYSCALE:
		pf = IW::PixelFormat::PF8GrayScale;
		break;
	case JCS_RGB:
	case JCS_CMYK:
		pf = IW::PixelFormat::PF24;
		break;
	default:
		return false;
	}

	// Adobe records CMYK inverted and libjpeg hands the samples over untouched,
	// so the APP14 marker is the only thing that says which way round they are.
	const bool bInvertedCmyk = pDecompress->saw_Adobe_marker != 0;

	CRect rcPage(0, 0, cxIn, cyIn);
	pImageOut->CreatePage(rcPage, pf, true);

	UINT nLineSize = pDecompress->output_components * cxIn;
	IW::CBuffer<BYTE> pBuffer(nLineSize * 4);
	BYTE* pLines[4] = {pBuffer, pBuffer + nLineSize, pBuffer + (nLineSize * 2), pBuffer + (nLineSize * 3)};
	// SetBitmap copies GetStorageWidth() bytes, which is DWORD-aligned and so can
	// exceed output_components * width.
	IW::CBuffer<BYTE> pBitsOut(IW::Max(static_cast<int>(nLineSize),
	                                   IW::CalcStorageWidth(static_cast<int>(cxIn), pf)));
	int nLinesIn = 0;
	int nLinesOut = 0;
	int nLine = 0;


	while (pDecompress->output_scanline < pDecompress->output_height)
	{
		nLinesIn += jpeg_read_scanlines(pDecompress, pLines, 4);
		nLine = 0;

		// Convert the out line to the correct
		while (nLinesOut < nLinesIn)
		{
			LPBYTE pBitsIn = pLines[nLine];

			switch (pDecompress->out_color_space)
			{
			case JCS_GRAYSCALE:
				IW::MemCopy(pBitsOut, pBitsIn, nLineSize);
				break;
			case JCS_RGB:
				IW::ConvertRGBtoBGR(pBitsOut, pBitsIn, cxIn);
				break;
			case JCS_CMYK:
				IW::ConvertCMYKtoBGR(pBitsOut, pBitsIn, cxIn, bInvertedCmyk);
				break;
			default:
				return false;
			}

			pImageOut->SetBitmap(nLinesOut, pBitsOut);

			nLinesOut++;
			nLine++;
		}

		if (pStatus->QueryCancel())
		{
			pStatus->SetError(App.LoadString(IDS_CANCELED));
			return false;
		}

		pStatus->Progress(pDecompress->output_scanline, pDecompress->output_height);
	}

	pImageOut->Flush();

	return true;
}


static void LoadJpegHeader(struct jpeg_decompress_struct* pDecompress, IW::IStreamIn* pStreamIn,
                           IW::IImageStream* pImageOut)
{
	// Step 2: specify data source (eg, a file) 
	jpeg_iw_src(pDecompress, pStreamIn, pImageOut);

	// The object is reused, and the transform path asks for every marker, so the
	// list has to be narrowed again rather than assumed empty.
	for (int m = 0; m < 16; m++)
		jpeg_save_markers(pDecompress, JPEG_APP0 + m, 0);

	jpeg_save_markers(pDecompress, JPEG_COM, 0);
	jpeg_save_markers(pDecompress, ICC_MARKER, 0xFFFF);
	jpeg_save_markers(pDecompress, IPTC_MARKER, 0xFFFF);
	jpeg_save_markers(pDecompress, XMP_EXIF_MARKER, 0xFFFF);

	// Step 3: read file parameters with jpeg_read_header() 
	if (JPEG_HEADER_OK != jpeg_read_header(pDecompress, TRUE))
	{
		throw IW::invalid_file();
	}

	// We skip images that are to big
	if (pDecompress->image_width > SHRT_MAX ||
		pDecompress->image_height > SHRT_MAX)
	{
		throw IW::invalid_file();
	}

	// set parameters for decompression 
	const CSize sizeThumbImage = pImageOut->GetThumbnailSize();
	const bool bWantThumbnail = sizeThumbImage.cx > 0 && sizeThumbImage.cy > 0;

	if (bWantThumbnail)
	{
		if ((static_cast<int>(pDecompress->image_width) > (sizeThumbImage.cx * 8)) &&
			(static_cast<int>(pDecompress->image_height) > (sizeThumbImage.cy * 8)))
		{
			pDecompress->scale_denom = 8;
		}
		else if ((static_cast<int>(pDecompress->image_width) > (sizeThumbImage.cx * 4)) &&
			(static_cast<int>(pDecompress->image_height) > (sizeThumbImage.cy * 4)))
		{
			pDecompress->scale_denom = 4;
		}
		else if ((static_cast<int>(pDecompress->image_width) > (sizeThumbImage.cx * 2)) &&
			(static_cast<int>(pDecompress->image_height) > (sizeThumbImage.cy * 2)))
		{
			pDecompress->scale_denom = 2;
		}
		else
		{
			// scale_denom is a divisor. Zero is not "no scaling": libjpeg-turbo's
			// ladder falls through every test and enlarges the image 2x.
			pDecompress->scale_denom = 1;
		}
	}

	pDecompress->dither_mode = JDITHER_NONE;
	pDecompress->two_pass_quantize = FALSE;
	pDecompress->quantize_colors = FALSE;

	switch (pDecompress->jpeg_color_space)
	{
	case JCS_GRAYSCALE:
		pDecompress->out_color_space = JCS_GRAYSCALE;
		break;
	case JCS_UNKNOWN:
	case JCS_RGB:
	case JCS_YCbCr:
	default:
		pDecompress->out_color_space = JCS_RGB;
		break;
	case JCS_CMYK:
	case JCS_YCCK:
		pDecompress->out_color_space = JCS_CMYK;
		break;
	}

	// set parameters for decompression 
	if (!bWantThumbnail)
	{
		pDecompress->dct_method = JDCT_FLOAT;
		pDecompress->do_fancy_upsampling = TRUE;
		pDecompress->do_block_smoothing = TRUE;
	}
	else
	{
		pDecompress->dct_method = JDCT_FASTEST;
		pDecompress->do_fancy_upsampling = FALSE;
		pDecompress->do_block_smoothing = FALSE;
	}

	if (!jpeg_start_decompress(pDecompress))
	{
		throw IW::invalid_file();
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadJpg::CLoadJpg() : _pStatus(IW::CNullStatus::Instance)
{
	// Initialize the JPEG decompression object with default error handling.
	m_decompress.err = jpeg_iw_error(&m_jerrDecompress);
	jpeg_create_decompress(&m_decompress);
	m_decompress.client_data = this;

	// Initialize the JPEG compression object with default error handling.
	m_compress.err = jpeg_iw_error(&m_jerrCompress);
	jpeg_create_compress(&m_compress);
	m_compress.client_data = this;

	// Attributes 
	m_transformcode = JXFORM_NONE;
	m_bCrop = false;
	_nOrientation = IW::Orientation::TopLeft;
}

CLoadJpg::~CLoadJpg()
{
	// This is an important step since it will release a good deal of memory. 
	jpeg_destroy_decompress(&m_decompress);
	jpeg_destroy_compress(&m_compress);
}

bool CLoadJpg::Read(const CString& str, IW::IStreamIn* pStreamIn, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	bool bSucceeded = false;

	try
	{
		_pStatus = pStatus;

		IW::CameraSettings settings;
		LoadJpegHeader(&m_decompress, pStreamIn, pImageOut);
		IW::MetaData iptc(IW::MetaDataTypes::PROFILE_IPTC);
		IW::MetaData xmp(IW::MetaDataTypes::PROFILE_XMP);

		for (jpeg_saved_marker_ptr marker = m_decompress.marker_list; marker != nullptr; marker = marker->next)
		{
			if (IsXmpBlob(marker))
			{
				xmp = LoadXmpBlob(marker);
			}
			else if (IsExifBlob(marker))
			{
				Exif::Parse(&settings, marker->data, marker->data_length);
				pImageOut->AddBlob(LoadExifBlob(marker));
			}
		}

		// An ICC profile spans as many APP2 markers as it needs, and a Photoshop
		// APP13 as many APP13s, so both are gathered from the whole list rather
		// than one marker at a time.
		iptc = LoadIptcProfile(m_decompress.marker_list);

		IW::MetaData icc = LoadIccProfile(m_decompress.marker_list);
		if (icc.GetDataSize() > 0) pImageOut->AddBlob(icc);

		pImageOut->AddMetaDataBlob(iptc, xmp);

		///////////////////////////////////
		/// Final meta data settings
		pImageOut->SetLoaderName(GetKey());

		CString strInformation;
		IW::PixelFormat pf = m_decompress.output_components == 1
			                     ? IW::PixelFormat::PF8GrayScale
			                     : IW::PixelFormat::PF24;

		if (m_decompress.saw_JFIF_marker)
		{
			strInformation.Format(_T("%dx%dx%d Jpeg JFIF %d.%d"),
			                      m_decompress.image_width, m_decompress.image_height, pf.ToBpp(),
			                      m_decompress.JFIF_major_version,
			                      m_decompress.JFIF_minor_version);
		}
		else
		{
			strInformation.Format(_T("%dx%dx%d Jpeg"),
			                      m_decompress.image_width, m_decompress.image_height, pf.ToBpp());
		}

		pImageOut->SetStatistics(strInformation);

		settings.OriginalImageSize.cx = m_decompress.image_width;
		settings.OriginalImageSize.cy = m_decompress.image_height;
		settings.OriginalBpp = pf;

		/////////////////////////////
		//// Decode the image

		// Set the resolution
		if (m_decompress.density_unit == 1)
		{
			// dots/inch				
			settings.XPelsPerMeter = IW::InchToMeter(m_decompress.X_density);
			settings.YPelsPerMeter = IW::InchToMeter(m_decompress.Y_density);
		}
		else if (m_decompress.density_unit == 2)
		{
			// dots/cm
			settings.XPelsPerMeter = IW::CMToMeter(m_decompress.X_density);
			settings.YPelsPerMeter = IW::CMToMeter(m_decompress.Y_density);
		}

		if (pImageOut->WantBitmap())
		{
			if (!LoadJpegImage(&m_decompress, pImageOut, pStatus))
			{
				// Cancelled or undecodable. Finishing here would raise a second,
				// misleading error over the one already reported.
				jpeg_abort((j_common_ptr)&m_decompress);
				_pStatus = IW::CNullStatus::Instance;
				return false;
			}

			// All done
			jpeg_finish_decompress(&m_decompress);
		}
		else
		{
			// We loded thumbnail instead so abort
			jpeg_abort((j_common_ptr)&m_decompress);
		}

		if (pImageOut->WantRawImage())
		{
			// Store the raw image for loss-less processing
			pImageOut->AddImageData(pStreamIn, IW::MetaDataTypes::JPEG_IMAGE);
		}


		pImageOut->SetCameraSettings(settings);

		bSucceeded = true;
	}
	catch (const std::exception& e)
	{
		const CString strWhat(CA2T(e.what()));
		pStatus->SetError(strWhat);
		ATLTRACE(_T("Exception in CLoadJpg::Read %s"), static_cast<LPCTSTR>(strWhat));

		jpeg_abort((j_common_ptr)&m_decompress);
	}

	_pStatus = IW::CNullStatus::Instance;

	return bSucceeded;
}

bool CLoadJpg::Write(const CString& strType, IW::IStreamOut* pStreamOut, const IW::Image& imageIn, const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	bool bSucceeded = false;

	try
	{
		// If we have the jped data stored losslessly
		// then we just stream that out :)
		if (!imageIn.IsEmpty() && imageIn.HasMetaData(IW::MetaDataTypes::JPEG_IMAGE))
		{
			// First we need to update the meta data blocks
			// The data will be written at the same time
			CJpegTransformation trans(pStreamOut, imageIn, pStatus);
			imageIn.IterateMetaData(&trans);

			// The caller has already truncated whatever it is saving over, so a
			// transform that could not run must not be reported as a save.
			bSucceeded = trans._bSuccess;
		}
		else
		{
			_pStatus = pStatus;
			jpeg_iw_dest(&m_compress, pStreamOut);

			const IW::Page pageIn = imageIn.GetFirstPage();
			const bool bGrayScale = pageIn.GetPixelFormat() == IW::PixelFormat::PF8GrayScale;

			UINT nWidth = m_compress.image_width = pageIn.GetWidth(); // image width and height, m_decompress pixels
			UINT nHeight = m_compress.image_height = pageIn.GetHeight();
			m_compress.input_components = bGrayScale ? 1 : 3; // # of color components per pixel
			m_compress.in_color_space = bGrayScale ? JCS_GRAYSCALE : JCS_RGB; // colorspace of input image 			

			// Now use the library's routine to set default compression parameters.
			// (You must set at least cinfo.in_color_space before calling this,
			// since the defaults depend on the source color space.)

			jpeg_set_defaults(&m_compress);
			// Now you can set any non-default parameters you wish to.
			// Here we just illustrate the use of quality (CQuantization table) scaling:

			// JFIF has unsigned 16-bit density fields; never wrap high DPI.
			m_compress.X_density = IW::JfifDensity(imageIn.GetXPelsPerMeter());
			m_compress.Y_density = IW::JfifDensity(imageIn.GetYPelsPerMeter());
			m_compress.density_unit = 1; // dots / inch

			const int nQuality = IW::Clamp(static_cast<int>(settings.JpegQuality), 1, 100);
			jpeg_set_quality(&m_compress, nQuality, TRUE); // limit to baseline-JPEG values

			// Above about 90 the chroma subsampling, not the quantisation table,
			// is what limits the result -- so asking for near-lossless has to turn
			// it off or the extra file size buys nothing.
			if (nQuality >= 90 && !bGrayScale)
			{
				for (int ci = 0; ci < m_compress.num_components; ci++)
				{
					m_compress.comp_info[ci].h_samp_factor = 1;
					m_compress.comp_info[ci].v_samp_factor = 1;
				}
			}

			// Enable entropy parm optimization.
			m_compress.optimize_coding = settings.JpegOptimize ? TRUE : FALSE;

			// TRUE ensures that we will write a complete interchange-JPEG file.
			// Pass TRUE unless you are very sure of what you're doing.

			// Select simple progressive mode.
			if (settings.JpegProgressive)
			{
				jpeg_simple_progression(&m_compress);
			}

			jpeg_start_compress(&m_compress, TRUE);

			// Write meta data into list
			if (!imageIn.IsEmpty())
			{
				_nOrientation = imageIn.GetCameraSettings().Orientation;
				imageIn.IterateMetaData(this);
			}

			// Here we use the library's state variable cinfo.next_scanline as the
			// loop counter, so that we don't have to keep track ourselves.
			// To keep things simple, we pass one scanline per call; you can pass
			// more if you wish, though.
			JSAMPARRAY buffer = (*m_compress.mem->alloc_sarray)((j_common_ptr)&m_compress, JPOOL_IMAGE,
			                                                    m_compress.image_width * m_compress.input_components,
			                                                    1);

			IW::CBuffer<COLORREF> pRGBALine(nWidth);
			IW::ConstIImageSurfaceLockPtr pLock = pageIn.GetSurfaceLock();

			for (UINT y = 0; y < nHeight; y++)
			{
				assert(m_compress.next_scanline < m_compress.image_height);

				pLock->RenderLine(pRGBALine, y, 0, nWidth);

				auto p = buffer[0];

				if (bGrayScale)
				{
					for (UINT x = 0; x < nWidth; x++)
						*p++ = static_cast<BYTE>(pRGBALine[x] >> 8);
				}
				else
				{
					for (UINT x = 0; x < nWidth; x++)
					{
						*p++ = static_cast<BYTE>(pRGBALine[x] >> 16);
						*p++ = static_cast<BYTE>(pRGBALine[x] >> 8);
						*p++ = static_cast<BYTE>(pRGBALine[x]);
					}
				}

				jpeg_write_scanlines(&m_compress, buffer, 1);

				if (pStatus->QueryCancel())
				{
					throw IW::invalid_file();
				}

				pStatus->Progress(y, nHeight);
			}

			jpeg_finish_compress(&m_compress);

			bSucceeded = true;
		}
	}
	catch (const std::exception& e)
	{
		const CString strWhat(CA2T(e.what()));
		pStatus->SetError(strWhat);
		ATLTRACE(_T("Exception in CLoadJpg::Read2 %s"), static_cast<LPCTSTR>(strWhat));

		jpeg_abort((j_common_ptr)&m_compress);
	}

	_pStatus = IW::CNullStatus::Instance;

	return bSucceeded;
}


bool CLoadJpg::Write(IW::IStreamOut* pStreamOut, IW::IStreamIn* pStreamIn, const IW::Image& imageIn,
                     const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	CString str;
	str.Format(IDS_ENCODING_FMT, static_cast<LPCTSTR>(GetTitle()));
	pStatus->SetStatusMessage(str);

	bool bSucceeded = false;

	jvirt_barray_ptr* src_coef_arrays;
	jvirt_barray_ptr* dst_coef_arrays;

	try
	{
		// Transform. Trimming would throw away the part of an edge block that a
		// quarter turn cannot move, which is real picture the user still has.
		m_transformoption.trim = FALSE;
		m_transformoption.force_grayscale = false;
		m_transformoption.transform = m_transformcode;

		if (m_bCrop)
		{
			m_transformoption.crop = TRUE;

			m_transformoption.crop_width = _rectCrop.right - _rectCrop.left;
			m_transformoption.crop_height = _rectCrop.bottom - _rectCrop.top;
			m_transformoption.crop_xoffset = _rectCrop.left;
			m_transformoption.crop_yoffset = _rectCrop.top;

			m_transformoption.crop_width_set = JCROP_POS;
			m_transformoption.crop_height_set = JCROP_POS;
			m_transformoption.crop_xoffset_set = JCROP_POS;
			m_transformoption.crop_yoffset_set = JCROP_POS;
		}
		else
		{
			m_transformoption.crop = FALSE;
			m_transformoption.crop_width_set = JCROP_UNSET;
			m_transformoption.crop_height_set = JCROP_UNSET;
			m_transformoption.crop_xoffset_set = JCROP_UNSET;
			m_transformoption.crop_yoffset_set = JCROP_UNSET;
		}

		// Specify data source for decompression
		jpeg_iw_src(&m_decompress, pStreamIn, nullptr);

		// Keep everything the source carried. The four profiles below are rewritten
		// from the image instead, so they are the only ones excluded.
		jcopy_markers_setup(&m_decompress, JCOPYOPT_ALL);

		// Read file header
		(void)jpeg_read_header(&m_decompress, TRUE);

		// Any space needed by a transform option must be requested before
		// jpeg_read_coefficients so that memory allocation will be done right.
		jtransform_request_workspace(&m_decompress, &m_transformoption);

		// Read source file as DCT coefficients
		src_coef_arrays = jpeg_read_coefficients(&m_decompress);

		if (!imageIn.IsEmpty())
		{
			m_decompress.X_density = IW::JfifDensity(imageIn.GetXPelsPerMeter(), true);
			m_decompress.Y_density = IW::JfifDensity(imageIn.GetYPelsPerMeter(), true);
			m_decompress.density_unit = 2; // dots / cm
		}

		// Initialize destination compression parameters from source values
		jpeg_copy_critical_parameters(&m_decompress, &m_compress);

		// Adjust destination parameters if required by transform options;
		// also find out which set of coefficient arrays will hold the output.
		dst_coef_arrays = jtransform_adjust_parameters(&m_decompress, &m_compress,
		                                               src_coef_arrays,
		                                               &m_transformoption);

		// Adjust default compression parameters
		m_compress.err->trace_level = 0;
		// Enable entropy parm optimization.
		m_compress.optimize_coding = (settings.JpegOptimize) ? TRUE : FALSE;

		// Select simple progressive mode.
		if (settings.JpegProgressive)
		{
			jpeg_simple_progression(&m_compress);
		}

		// Specify data destination for compression
		jpeg_iw_dest(&m_compress, pStreamOut);

		// Start compressor (note no image data is actually written here)
		jpeg_write_coefficients(&m_compress, dst_coef_arrays);

		// Comments, APP12 and anything else the source carried
		jcopy_markers_execute(&m_decompress, &m_compress, JCOPYOPT_ALL_EXCEPT_PROFILES);

		// Write meta data into list
		if (!imageIn.IsEmpty())
		{
			_nOrientation = imageIn.GetCameraSettings().Orientation;
			imageIn.IterateMetaData(this);
		}

		pStatus->SetStatusMessage(App.LoadString(IDS_JPEGTRANSFORM));

		// Execute image transformation, if any 
		jtransform_execute_transform(
			&m_decompress, &m_compress,
			src_coef_arrays,
			&m_transformoption,
			pStatus);

		// Finish compression and release memory 
		jpeg_finish_compress(&m_compress);
		jpeg_finish_decompress(&m_decompress);

		if (pStatus->QueryCancel()) return false;
		bSucceeded = true;
	}
	catch (const std::exception& e)
	{
		const CString strWhat(CA2T(e.what()));
		pStatus->SetError(strWhat);
		ATLTRACE(_T("Exception in CLoadJpg::Write %s"), static_cast<LPCTSTR>(strWhat));

		// Finish compression and release memory 
		jpeg_abort((j_common_ptr)&m_compress);
		jpeg_abort((j_common_ptr)&m_decompress);
	}

	return bSucceeded;
}

bool CLoadJpg::AddMetaDataBlob(const IW::MetaData& data)
{
	DWORD dwType = data.GetType();

	if (dwType == IW::MetaDataTypes::PROFILE_IPTC)
	{
		WriteIptcBlob(&m_compress, data);
	}
	else if (dwType == IW::MetaDataTypes::PROFILE_XMP)
	{
		WriteXmpBlob(&m_compress, data);
	}
	else if (dwType == IW::MetaDataTypes::PROFILE_ICC)
	{
		WriteIccBlob(&m_compress, data);
	}
	else if (dwType == IW::MetaDataTypes::PROFILE_EXIF)
	{
		WriteExifBlob(&m_compress, data, _nOrientation);
	}

	return true;
}

HWND CLoadJpg::CreateSettingsWindow(HWND hWndParent, IW::CodecSettings& settings)
{
	auto pPanel = new CJpegSettingsPanel(settings);
	const HWND hWnd = pPanel->Create(hWndParent);

	// OnFinalMessage is what deletes it, and it never runs if there is no window.
	if (hWnd == nullptr)
		delete pPanel;

	return hWnd;
}

LRESULT CJpegSettingsPanel::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	IW::ScopeLockedBool lockSetting(_bSetting);

	const HWND hwndTrack = GetDlgItem(IDC_QUALITY_SLIDER);
	::SendMessage(hwndTrack, TBM_SETRANGE, TRUE, MAKELONG(1, 100));
	::SendMessage(hwndTrack, TBM_SETPAGESIZE, 0, 10);
	::SendMessage(hwndTrack, TBM_SETLINESIZE, 0, 1);
	::SendMessage(hwndTrack, TBM_SETTICFREQ, 10, 0);
	::SendMessage(hwndTrack, TBM_SETPOS, TRUE, _settings.JpegQuality);

	SetDlgItemInt(IDC_QUALITY, _settings.JpegQuality, FALSE);
	CheckDlgButton(IDC_OPTIMIZE, _settings.JpegOptimize ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PROGRESSIVE, _settings.JpegProgressive ? BST_CHECKED : BST_UNCHECKED);

	return TRUE;
}

LRESULT CJpegSettingsPanel::OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	const auto hwndSlider = (HWND)lParam;

	if (GetDlgItem(IDC_QUALITY_SLIDER) != hwndSlider)
		return 0;

	IW::ScopeLockedBool lockSetting(_bSetting);

	_settings.JpegQuality = static_cast<long>(::SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
	SetDlgItemInt(IDC_QUALITY, _settings.JpegQuality, FALSE);

	return 0;
}

LRESULT CJpegSettingsPanel::OnQualityChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (_bSetting)
		return 0;

	IW::ScopeLockedBool lockSetting(_bSetting);

	CString str;
	GetDlgItemText(IDC_QUALITY, str);

	// The edit is not rewritten here: half-typed numbers are the user's to finish.
	_settings.JpegQuality = IW::Clamp(_ttoi(str), 1, 100);
	::SendMessage(GetDlgItem(IDC_QUALITY_SLIDER), TBM_SETPOS, TRUE, _settings.JpegQuality);

	return 0;
}

LRESULT CJpegSettingsPanel::OnButtonChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (_bSetting)
		return 0;

	_settings.JpegOptimize = BST_CHECKED == IsDlgButtonChecked(IDC_OPTIMIZE);
	_settings.JpegProgressive = BST_CHECKED == IsDlgButtonChecked(IDC_PROGRESSIVE);

	return 0;
}
