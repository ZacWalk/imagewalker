// ImageWalker by Zac Walker
//
// Purpose: JPEG reader/writer over libjpeg-turbo, carrying the EXIF, IPTC,
//          XMP and ICC markers through.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "FileFormat.h"
#include "FileJpegTran.h"

class CLoadJpg;

class CLoadJpg :  public CLoad<CLoadJpg>
{
protected:

	// One error manager per object: they share nothing else, and msg_code and
	// num_warnings are per-image state.
	struct jpeg_error_mgr m_jerrDecompress;
	struct jpeg_error_mgr m_jerrCompress;
	struct jpeg_decompress_struct m_decompress;
	struct jpeg_compress_struct m_compress;

public:
	// Construction
	CLoadJpg();
	virtual ~CLoadJpg();

	// Details
	static CString _GetKey() { return _T("JointPhotographicExpertsGroupJFIF"); };
	static CString _GetTitle() { return CString(IW::Text::JPEG_TITLE); };
	static CString _GetDescription() { return CString(IW::Text::JPEG_DESC); };
	static CString _GetExtensionList() { return _T("JPG,JPEG,JPE"); };
	static CString _GetExtensionDefault() { return _T("JPG"); };
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::SAVE | IW::ImageLoaderFlags::METADATA | IW::ImageLoaderFlags::EXIF | IW::ImageLoaderFlags::ICC | IW::ImageLoaderFlags::HTML; };

	bool Read(const CString &strType, IW::IStreamIn *pStreamIn,	IW::IImageStream *pImageOut, IW::IStatus *pStatus);
	bool Write(const CString &strType,	IW::IStreamOut *pStreamOut,	const IW::Image &imageIn, const IW::CodecSettings& settings, IW::IStatus *pStatus);
	bool Write(IW::IStreamOut *pStreamOut,	IW::IStreamIn *pStreamIn,	const IW::Image &imageIn,	const IW::CodecSettings& settings, IW::IStatus *pStatus);


	// Status log
	IW::IStatus *_pStatus;

	bool m_bCrop;

	// What the EXIF orientation tag has to say once this image is on disk. The
	// pixels being written are whatever the image holds, so the tag has to match
	// them rather than being assumed upright.
	int _nOrientation;

	jpeg_transform_info m_transformoption; // image transformation options
	JXFORM_CODE m_transformcode;
	CRect _rectCrop;

	// Blobs. Used to store meta data loaded from
	bool AddMetaDataBlob(const IW::MetaData &data);
};

class CJpegTransformation
{
public:
	IW::ScopeObj<CLoadJpg> _loader;
	IW::IImageStream *m_pImageOut;
	IW::Image _imageIn;
	IW::IStatus *_pStatus;
	IW::IStreamOut *m_pStreamOut;

	// A loss-less transform re-encodes nothing, so the defaults are all it needs.
	IW::CodecSettings _settings;

	bool _bSuccess;

	CJpegTransformation(JXFORM_CODE code,  IW::IImageStream *pImageOut, const IW::Image &imageIn, IW::IStatus *pStatus) :
		m_pStreamOut(0),
		m_pImageOut(pImageOut),
		_imageIn(imageIn),
		_pStatus(pStatus),
		_bSuccess(false)
	{
		_loader.m_transformcode = code;
	}

	CJpegTransformation(CRect &rcCrop, IW::IImageStream *pImageOut, const IW::Image &imageIn, IW::IStatus *pStatus) :
		m_pStreamOut(0),
		m_pImageOut(pImageOut),
		_imageIn(imageIn),
		_pStatus(pStatus),
		_bSuccess(false)
	{
		_loader.m_transformcode = JXFORM_NONE;
		_loader.m_bCrop = true;
		_loader._rectCrop = rcCrop;
	}

	CJpegTransformation(IW::IStreamOut *pStreamOut, const IW::Image &imageIn, IW::IStatus *pStatus) :
		m_pStreamOut(pStreamOut),
		m_pImageOut(0),
		_imageIn(imageIn),
		_pStatus(pStatus),
		_bSuccess(false)
	{
		_loader.m_transformcode = JXFORM_NONE;
	}

	bool AddMetaDataBlob(const IW::MetaData &data)
	{
		DWORD dwType = data.GetType();

		if (IW::MetaDataTypes::JPEG_IMAGE == dwType)
		{
			try
			{
				IW::StreamConstBlob  streamIn(data);

				if (m_pStreamOut)
				{
					// Write to stream
					if (_loader.Write(m_pStreamOut, &streamIn, _imageIn, _settings, _pStatus))
					{
						_bSuccess = true;
					}
				}
				else
				{
					// Write to Image
					IW::SimpleBlob encodedData;
					IW::StreamBlob<IW::SimpleBlob>  streamOut(encodedData);

					if (_loader.Write(&streamOut, &streamIn, _imageIn, _settings, _pStatus))
					{
						streamOut.Seek(IW::IStreamCommon::eBegin, 0);

						if (_loader.Read(g_szEmptyString, &streamOut, m_pImageOut, _pStatus))
						{
							_bSuccess = true;
						}
					}
				}

			}
			catch(std::exception &e)
			{
				const CString strWhat = e.what();
				_pStatus->SetError(strWhat);
				ATLTRACE(_T("Exception in CJpegTransformation::AddMetaDataBlob %s"), static_cast<LPCTSTR>(strWhat));
			}
		}

		return true;
	}
};
