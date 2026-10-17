// ImageWalker by Zac Walker
//
// Purpose: Batch transforms over a selection - lossless for JPEG,
//          decode-and-re-encode otherwise.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once 

#include "FileJpegTran.h"
#include "ImagingStreams.h"
#include "Metadata.h"

// Which way the lossless path went. "Not a JPEG" is the only outcome that may
// fall through to a full re-encode: a cancel or a failure part way through one
// would otherwise overwrite the file with a lossy copy nobody asked for.
enum class JpegTransformResult
{
	NotJpeg,
	Done,
	Failed
};

template<class T>
class JpegTransformer
{
private:
	jpeg_transform_info m_transformoption; 

	struct jpeg_decompress_struct m_srcinfo;
	struct jpeg_compress_struct m_dstinfo;
	struct jpeg_error_mgr m_jsrcerr, m_jdsterr;

	jvirt_barray_ptr * src_coef_arrays;
	jvirt_barray_ptr * dst_coef_arrays;

	// The turn the user asked for. m_transformoption.transform is derived from it
	// per file, because the file's own EXIF orientation is folded into it.
	JXFORM_CODE _codeRequested;

	CString _strJpegTransform;

public:

	METHODDEF(void) ima_jpeg_error_exit (j_common_ptr cinfo)
	{
		char sz[JMSG_LENGTH_MAX];
		(*cinfo->err->format_message) (cinfo, sz);
		throw IW::invalid_file(sz);
	}

	JpegTransformer(IW::Rotation::Direction direction) : _codeRequested(JXFORM_NONE)
	{
		_strJpegTransform.LoadString(IDS_JPEGTRANSFORM);

		// Initialize the JPEG decompression object with default error handling.
		m_srcinfo.err = jpeg_std_error(&m_jsrcerr);
		m_jsrcerr.error_exit = ima_jpeg_error_exit;
		jpeg_create_decompress(&m_srcinfo);

		// Initialize the JPEG compression object with default error handling.
		m_dstinfo.err = jpeg_std_error(&m_jdsterr);
		m_jdsterr.error_exit = ima_jpeg_error_exit;
		jpeg_create_compress(&m_dstinfo);

		// Set Options. Trimming would discard the part of an edge block a quarter
		// turn cannot move -- up to fifteen rows of the user's picture, silently,
		// every time they press the button.
		m_transformoption.transform = JXFORM_NONE;
		m_transformoption.trim = FALSE;
		m_transformoption.force_grayscale = FALSE;
		m_transformoption.crop = FALSE;
		m_transformoption.crop_width_set = JCROP_UNSET;
		m_transformoption.crop_height_set = JCROP_UNSET;
		m_transformoption.crop_xoffset_set = JCROP_UNSET;
		m_transformoption.crop_yoffset_set = JCROP_UNSET;

		if (direction == IW::Rotation::Left)
		{
			_codeRequested = JXFORM_ROT_270;
		}
		else if (direction == IW::Rotation::Right)
		{
			_codeRequested = JXFORM_ROT_90;
		}

		m_jsrcerr.trace_level = m_jdsterr.trace_level;
		m_srcinfo.mem->max_memory_to_use = m_dstinfo.mem->max_memory_to_use;
	}

	~JpegTransformer()
	{
		jpeg_destroy_compress(&m_dstinfo);
		jpeg_destroy_decompress(&m_srcinfo);
	}

	JpegTransformResult TransformJpeg(IW::FolderItem *pItem, IW::IStatus *pStatus)
	{
		// Anything that goes wrong before the header could just be a file that is
		// not a JPEG. After it, this was a JPEG and the caller must not try again.
		bool bHeaderRead = false;

		try
		{
			IW::FolderStreamIn streamIn(pItem);	

			// Specify data source for decompression
			jpeg_iw_src(&m_srcinfo, &streamIn, 0);

			// Enable saving of extra markers that we want to copy
			jcopy_markers_setup(&m_srcinfo, JCOPYOPT_ALL);

			// Read file header
			if (JPEG_HEADER_OK == jpeg_read_header(&m_srcinfo, TRUE))
			{
				bHeaderRead = true;

				const int maxSteps = 8;
				pStatus->SetStatusMessage(_strJpegTransform);

				// One turn, however the camera stored the picture: the orientation
				// tag is folded into the transform and then cleared.
				m_transformoption.transform = _codeRequested;
				jtransform_normalise_exif_orientation(&m_srcinfo, &m_transformoption);

				// Any space needed by a transform option must be requested before
				// jpeg_read_coefficients so that memory allocation will be done right.
				jtransform_request_workspace(&m_srcinfo, &m_transformoption);

				pStatus->Progress(1, maxSteps);

				// Read source file as DCT coefficients
				src_coef_arrays = jpeg_read_coefficients(&m_srcinfo);

				pStatus->Progress(2, maxSteps);

				// Initialize destination compression parameters from source values
				jpeg_copy_critical_parameters(&m_srcinfo, &m_dstinfo);

				pStatus->Progress(3, maxSteps);

				// Adjust destination parameters if required by transform options;
				// also find out which set of coefficient arrays will hold the output.
				dst_coef_arrays = jtransform_adjust_parameters(&m_srcinfo, &m_dstinfo, src_coef_arrays, &m_transformoption);

				pStatus->Progress(4, maxSteps);

				// Specify data destination for compression
				IW::FolderStreamOut streamOut(pItem);
				jpeg_iw_dest(&m_dstinfo, &streamOut);

				pStatus->Progress(5, maxSteps);


				// Start compressor (note no image data is actually written here)
				jpeg_write_coefficients(&m_dstinfo, dst_coef_arrays);

				pStatus->Progress(6, maxSteps);

				// Copy to the output file any extra markers that we want to preserve
				WriteMarkers(&m_srcinfo, &m_dstinfo);				

				pStatus->Progress(7, maxSteps);

				// Execute image transformation, if any 
				jtransform_execute_transform(&m_srcinfo, &m_dstinfo, src_coef_arrays, &m_transformoption, pStatus);	

				pStatus->Progress(8, maxSteps);

				streamIn.Close(pStatus);

				// A cancel leaves the coefficient arrays half filled, so the
				// output must be thrown away rather than put over the original.
				if (pStatus->QueryCancel())
				{
					streamOut.Abort();
					jpeg_abort((j_common_ptr) &m_dstinfo);
					jpeg_abort((j_common_ptr) &m_srcinfo);
					return JpegTransformResult::Failed;
				}

				jpeg_finish_compress(&m_dstinfo);

				if (!streamOut.Close(pStatus))
				{
					jpeg_abort((j_common_ptr) &m_srcinfo);
					return JpegTransformResult::Failed;
				}
			}

			jpeg_finish_decompress(&m_srcinfo);
		}
		catch(std::exception &e)
		{
			CString strError = e.what();

			// Finish compression and release memory 
			jpeg_abort((j_common_ptr) &m_dstinfo);
			jpeg_abort((j_common_ptr) &m_srcinfo);

			if (bHeaderRead)
			{
				pStatus->SetError(strError);
				return JpegTransformResult::Failed;
			}

			return JpegTransformResult::NotJpeg;
		}

		return bHeaderRead ? JpegTransformResult::Done : JpegTransformResult::NotJpeg;
	}	

	// Every marker the source carried goes out unchanged. Reassembling APP13 and
	// the XMP packet from a model that only understood part of them is what used
	// to drop Photoshop resource blocks and any XMP outside Dublin Core.
	void WriteMarkers(j_decompress_ptr srcinfo, j_compress_ptr dstinfo)
	{
		for (jpeg_saved_marker_ptr marker = srcinfo->marker_list; marker != nullptr; marker = marker->next) 
		{
			if (dstinfo->write_JFIF_header &&
				marker->marker == JPEG_APP0 &&
				marker->data_length >= 5 &&
				GETJOCTET(marker->data[0]) == 0x4A &&
				GETJOCTET(marker->data[1]) == 0x46 &&
				GETJOCTET(marker->data[2]) == 0x49 &&
				GETJOCTET(marker->data[3]) == 0x46 &&
				GETJOCTET(marker->data[4]) == 0)
			{
				continue;			// reject duplicate JFIF 
			}

			if (dstinfo->write_Adobe_marker &&
				marker->marker == JPEG_APP0 + 14 &&
				marker->data_length >= 5 &&
				GETJOCTET(marker->data[0]) == 0x41 &&
				GETJOCTET(marker->data[1]) == 0x64 &&
				GETJOCTET(marker->data[2]) == 0x6F &&
				GETJOCTET(marker->data[3]) == 0x62 &&
				GETJOCTET(marker->data[4]) == 0x65)
			{
				continue;			// reject duplicate Adobe
			}

			jpeg_write_marker(dstinfo, marker->marker, marker->data, marker->data_length);
		}
	}
};

template<class T>
class ImageTransformer
{
public:
	CLoadAny _loader;	

	ImageTransformer(ImageLoaders &loaders) : _loader(loaders)
	{
	}

	bool StartFolder(IW::Folder *pFolder, IW::IStatus *pStatus) 
	{ 
		return true; 
	};
	
	bool StartItem(IW::FolderItem *pItem, IW::IStatus *pStatus)
	{
		T *pT = static_cast<T*>(this);

		switch (pT->TransformJpeg(pItem, pStatus))
		{
		case JpegTransformResult::Done:
			return true;

		case JpegTransformResult::Failed:
			return false;

		default:
			break;
		}

		return pT->TransformItem(pItem, pStatus);
	}

	bool EndItem() 
	{ 
		return true; 
	};

	bool EndFolder() 
	{ 
		return true; 
	};

	bool TransformItem(IW::FolderItem *pItem, IW::IStatus *pStatus)
	{
		T *pT = static_cast<T*>(this);
		CString str;
		IW::Image imageIn = pItem->OpenAsImage(_loader, pStatus);

		if (!imageIn.IsEmpty())
		{
			const CString strKey = imageIn.GetLoaderName();

			if (IW::ImageLoaderFlags::SAVE & _loader.GetFlags(strKey))
			{				
				IW::Image imageOut;

				if (pT->Transform(imageIn, imageOut, pStatus))
				{
					if (pItem->SaveAsImage(_loader, imageOut, imageOut.GetLoaderName(), pStatus))
					{
						return true;
					}
					else
					{
						str.Format(_T("Could not save '%s' as an image."), static_cast<LPCTSTR>(pItem->GetFileName()));
						pStatus->SetError(str);
					}
				}
			}
			else
			{
				str.Format(_T("Saving is not supported for '%s' and images of its type."), static_cast<LPCTSTR>(pItem->GetFileName()));
				pStatus->SetError(str);
			}
		}
		else
		{
			str.Format(_T("Could not open '%s' as an image."), static_cast<LPCTSTR>(pItem->GetFileName()));
			pStatus->SetError(str);
		}

		return false;
	}	
};

class ItemRotater : 
	public ImageTransformer<ItemRotater>, 
	public JpegTransformer<ItemRotater>
{
private:
	IW::Rotation::Direction _direction;

public:
	ItemRotater(ImageLoaders &loaders, IW::Rotation::Direction direction) : 
		_direction(direction), 
		JpegTransformer<ItemRotater>(direction), 
		ImageTransformer<ItemRotater>(loaders)
	{
	};

	bool Transform(IW::Image &imageIn, IW::Image &imageOut, IW::IStatus *pStatus)
	{
		IW::IterateImageMetaData(imageIn, imageOut, pStatus);

		// Rotate90 is the clockwise transpose, so Left is Rotate270 -- the same
		// turn the lossless path takes as JXFORM_ROT_270.
		if (_direction == IW::Rotation::Left)
		{
			return IW::Rotate270(imageIn, imageOut, pStatus);
		}
		else if (_direction == IW::Rotation::Right)
		{
			return IW::Rotate90(imageIn, imageOut, pStatus);
		}

		return false;
	}
};
