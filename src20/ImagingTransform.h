// ImageWalker by Zac Walker
//
// Purpose: Batch transforms over a selection - lossless for JPEG,
//          decode-and-re-encode otherwise.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/imaging/FileJpegTran.h"
#include "ImagingStreams.h"
#include "iw/imaging/Metadata.h"

#include "iw/imaging/JpegTransform.h"

template<class T>
class JpegTransformer
{
public:
	explicit JpegTransformer(IW::Rotation::Direction direction)
		: transform_(direction == IW::Rotation::Left ? JXFORM_ROT_270 :
			direction == IW::Rotation::Right ? JXFORM_ROT_90 : JXFORM_NONE) {}

	JpegTransformResult TransformJpeg(IW::FolderItem* item, IW::IStatus* status)
	{
		try
		{
			IW::FolderStreamIn input(item);
			if (!IW::IsJpegStream(input)) return JpegTransformResult::NotJpeg;
			IW::FolderStreamOut output(item);
			const auto result = IW::TransformJpegStream(input, output, transform_, status);
			if (result != JpegTransformResult::Done) return result;
			if (!input.Close(status) || status->QueryCancel() || !output.Close(status))
				return JpegTransformResult::Failed;
			return JpegTransformResult::Done;
		}
		catch (const std::exception& error)
		{
			status->SetError(CString(error.what()));
			return JpegTransformResult::Failed;
		}
	}
private:
	JXFORM_CODE transform_;
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
