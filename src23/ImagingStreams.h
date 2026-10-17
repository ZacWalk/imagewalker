// ImageWalker by Zac Walker
//
// Purpose: IImageStream: the push interface a format writes into while
//          decoding, and the adapters that scale or thumbnail on the way
//          through.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once
#include "Metadata.h"
#include "ViewModelSearch.h"

namespace IW
{
	// IW::IImageStream
	template<class T>
	inline void IterateImageMetaData(const Image &imageIn, T &out, IW::IStatus *pStatus)
	{
		out.SetTitle(imageIn.GetTitle());
		out.SetTags(imageIn.GetTags());
		out.SetDescription(imageIn.GetDescription());
		out.SetObjectName(imageIn.GetObjectName());
		out.SetStatistics(imageIn.GetStatistics());
		out.SetLoaderName(imageIn.GetLoaderName());
		out.SetCameraSettings(imageIn.GetCameraSettings());

		for(IW::Image::BLOBLIST::const_iterator iBlob = imageIn.Blobs.begin(); iBlob != imageIn.Blobs.end(); ++iBlob)
		{
			if (!iBlob->IsImageData())
			{
				out.SetMetaData(*iBlob);
			}
		}
	}

	template<class T>
	inline void IterateImage(const Image &imageIn, T &out, IW::IStatus *pStatus)
	{
		for(IW::Image::PageList::const_iterator page = imageIn.Pages.begin(); page != imageIn.Pages.end(); ++page)
		{
			const CRect rc = page->GetPageRect();
			const IW::PixelFormat pf = page->GetPixelFormat();
			const int nFlags = page->GetFlags();

			out.CreatePage(rc, pf, true);

			if (pf.HasPalette())
			{
				out.SetPalette(page->GetPalette());
			}

			if (nFlags & IW::PageFlags::HasTransparent) out.SetTransparent(page->GetTransparent()); 
			if (nFlags & IW::PageFlags::HasBackGround) out.SetBackGround(page->GetBackGround()); 
			if (nFlags & IW::PageFlags::HasTimeDelay) out.SetTimeDelay(page->GetTimeDelay()); 
			out.SetDisposalMethod(page->GetDisposalMethod());

			int nHeight = page->GetHeight();

			for (int y = 0; y < nHeight; y++) 
			{
				LPCBYTE pBits = page->GetBitmapLine(y);
				out.SetBitmap(y, pBits);
			}

			out.Flush();
		}
	}

	class CNull
	{
	};

	template<class TBase, bool tWantRawImage = true>
	class ImageStream : public TBase
	{
	protected:
		IW::Image &_image;
		IW::Page _page;

	public:


		ImageStream(IW::Image &image) : _image(image)
		{
		}

		~ImageStream()
		{
			Flush();
		}

		void CreatePage(const CRect &rc, const PixelFormat &pf, bool bIsSequential)
		{
			_page = _image.CreatePage(rc, pf);
		}

		void Flush()
		{
			_image.FixOrientation(IW::CNullStatus::Instance);
		}

		void SetBitmap(int y, LPCVOID pBitsIn)
		{
			LPVOID p = _page.GetBitmapLine(y);
			IW::MemCopy(p, pBitsIn, _page.GetStorageWidth());
		}

		void SetPalette(LPCCOLORREF pPalette)
		{
			LPVOID p = _page.GetPalette();
			PixelFormat pf = _page.GetPixelFormat();
			IW::MemCopy(p, pPalette, sizeof(COLORREF) * pf.NumberOfPaletteEntries());
		}

		// Attributes
		CSize GetThumbnailSize()
		{
			return CSize(0, 0);
		}

		bool WantBitmap()
		{
			return true;
		}

		bool WantRawImage()
		{
			return tWantRawImage;
		}

		// Image Attributes
		void SetImageFlags(const DWORD dw)
		{
			_image.SetFlags(_image.GetFlags() | dw);
		}	

		void SetTimeDelay(DWORD dw)
		{
			_page.SetTimeDelay(dw);
		}

		void SetDisposalMethod(DWORD dw)
		{
			_page.SetDisposalMethod(dw);
		}

		void SetBackGround(DWORD dw)
		{
			_page.SetBackGround(dw);
		}

		void SetTransparent(DWORD dw)
		{
			_page.SetTransparent(dw);
		}

		void SetCameraSettings(const CameraSettings &settings)
		{
			_image.SetCameraSettings(settings);
		}

		// Meta Data Attributes
		void SetTitle(const CString &str)
		{
			_image.SetTitle(str);
		}

		void SetTags(const CString &str)
		{
			_image.SetTags(str);
		}

		void SetDescription(const CString &str)
		{
			_image.SetDescription(str);
		}
		
		void SetObjectName(const CString &str)
		{
			_image.SetObjectName(str);
		}

		void SetStatistics(const CString &str)
		{
			_image.SetStatistics(str);
		}

		void SetLoaderName(const CString &str)
		{
			_image.SetLoaderName(str);
		}

		void AddImageData(IW::IStreamIn *pStreamIn, DWORD dwType)
		{
			_image.AddImageData(pStreamIn, dwType);
		}

		void AddMetaDataBlob(IW::MetaData &iptc, IW::MetaData &xmp)
		{
			ImageMetaData meta(iptc, xmp);
			_image.SetTitle(meta.GetTitle());
			_image.SetTags(meta.GetTags());
			_image.SetDescription(meta.GetDescription());
			_image.SetObjectName(meta.GetObjectName());

			AddBlob(iptc);
			AddBlob(xmp);
		}

		void AddBlob(IW::MetaData &blob)
		{
			if (!blob.IsEmpty())
			{
				if (blob.IsType(IW::MetaDataTypes::PROFILE_EXIF)) SetImageFlags(IW::ImageFlags::HasExif);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_IPTC)) SetImageFlags(IW::ImageFlags::HasIPTC);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_XMP)) SetImageFlags(IW::ImageFlags::HasXmp);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_ICC)) SetImageFlags(IW::ImageFlags::HasICC);

				_image.SetMetaData(blob);
			}
		}
	};	

	template<class TBase, bool isHighQuality = false>
	class ImageStreamThumbnail : public TBase
	{
	protected:
		ImageStream<CNull> _imageStream;

		
		IW::LPRGBSUM _pRGBSum;
		bool _bNeedsThumbnailing;
		CRect _rectIn;
		CRect _rectOut;
		IW::PixelFormat _pf;
		COLORREF _palette[256];
		CSize _sizeThumbnail;
		DWORD _dwPageFlags;
		DWORD _dwImageFlags;
		bool _bPagePending;
		bool _bIsSequential;
		int _nLastYOut;
		int _nLastYIn;
		const Search::Spec &_search;
		IW::FileTime _ft;
		CString _strTitle;
		CString _strTags;
		CString _strDescription;

		void MoveSumToPage(int nLineLimit)
		{
			assert(_bNeedsThumbnailing);

			bool bHasAlpha = _pf.HasAlpha();
			IW::PixelFormat pf(bHasAlpha ? IW::PixelFormat::PF32Alpha : (isHighQuality ? IW::PixelFormat::PF32 : IW::PixelFormat::PF555));

			if (_nLastYOut == -1)
			{
				_imageStream.CreatePage(_rectOut, pf, true);
			}		

			const int cx = _rectOut.right - _rectOut.left;

			int nStorageWidth = IW::CalcStorageWidth(cx, pf);
			IW::CBuffer<BYTE> pLine(nStorageWidth);

			while(_nLastYOut < nLineLimit) 
			{		
				_nLastYOut++;
				const IW::RGBSUM *ps = _pRGBSum + (cx * _nLastYOut);		

				if (bHasAlpha || isHighQuality)
				{
					// 32 needed for alpha
					SumLineTo32(pLine, ps, cx);			
				}				
				else
				{
					SumLineTo16(pLine, ps, cx);
				}			

				_imageStream.SetBitmap(_nLastYOut, pLine);
			}
		}

	public:

		ImageStreamThumbnail(IW::Image &image, const Search::Spec &search, const CSize &sizeThumbnail  = App.Settings._sizeThumbImage) :  _imageStream(image), _sizeThumbnail(sizeThumbnail), _search(search)
		{
			int nSumAllocSize = sizeof(IW::RGBSUM) * _sizeThumbnail.cx * _sizeThumbnail.cy;
			_pRGBSum = static_cast<IW::LPRGBSUM>(IW::Alloc(nSumAllocSize));
			IW::MemZero(_pRGBSum, nSumAllocSize);

			_dwPageFlags = 0;
			_dwImageFlags = 0;
			_bPagePending = false;
			_bIsSequential = false;
			_nLastYOut = -1;
			_nLastYIn = -1;
			_bNeedsThumbnailing = false;
			IW::MemZero(_palette, sizeof(_palette));
		}

		~ImageStreamThumbnail()
		{
			Flush();	

			if (_pRGBSum)
			{
				IW::Free(_pRGBSum);
				_pRGBSum = 0;
			}
		}

		void Flush()
		{
			if (_bPagePending)
			{
				MoveSumToPage(_nLastYIn);				
			}	

			_imageStream.Flush();
		}

		void CreatePage(const CRect &rectIn, const IW::PixelFormat &pf, bool bIsSequential)
		{	
			Flush();			

			const int cxIn = rectIn.Width();
			const int cyIn = rectIn.Height();

			_bNeedsThumbnailing = _sizeThumbnail.cx < cxIn || _sizeThumbnail.cy < cyIn;

			if (!_bNeedsThumbnailing)
			{
				_imageStream.CreatePage(rectIn, pf, bIsSequential);
			}
			else
			{
				_nLastYOut = -1;
				_rectIn = rectIn;
				_pf = pf;
				_bIsSequential = bIsSequential;

				long nDiv = 0x1000;
				long sy = MulDiv(_sizeThumbnail.cy, nDiv, cyIn);
				long sx = MulDiv(_sizeThumbnail.cx, nDiv, cxIn);	
				long s = IW::Min(sy, sx);

				int cxOut = MulDiv(cxIn, s, nDiv);
				int cyOut = MulDiv(cyIn, s, nDiv);

				// MulDiv rounds to nearest, so the scaled extent can land a pixel
				// outside the box _pRGBSum was allocated for -- 200x200 against a
				// 6000x6000 source comes out 201x201.
				if (cxOut < 1) cxOut = 1;
				if (cyOut < 1) cyOut = 1;
				if (cxOut > _sizeThumbnail.cx) cxOut = _sizeThumbnail.cx;
				if (cyOut > _sizeThumbnail.cy) cyOut = _sizeThumbnail.cy;

				_rectOut.left = MulDiv(rectIn.left, s, nDiv);
				_rectOut.top = MulDiv(rectIn.top, s, nDiv);
				_rectOut.right = _rectOut.left + cxOut;
				_rectOut.bottom = _rectOut.top + cyOut;

				IW::MemZero(_pRGBSum, sizeof(IW::RGBSUM) * _sizeThumbnail.cx * _sizeThumbnail.cy);
				_bPagePending = true;
			}
		}

		void SetBitmap(int y, LPCVOID pBitsIn)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetBitmap(y, pBitsIn);
			}
			else
			{
				const int cxIn = _rectIn.Width();
				const int cyIn = _rectIn.Height();
				const int cxOut = _rectOut.Width();
				const int cyOut = _rectOut.Height();

				IW::CBuffer<DWORD> lineStorage(cxIn);
				LPDWORD pLineIn = lineStorage;
				LPCBYTE pLineSrc = static_cast<LPCBYTE>(pBitsIn);

				switch(_pf._pf)
				{
				case IW::PixelFormat::PF1:
					IW::Convert1to32(pLineIn, pLineSrc, 0, cxIn, _palette);
					break;
				case IW::PixelFormat::PF2:
					IW::Convert2to32(pLineIn, pLineSrc, 0, cxIn, _palette);
					break;
				case IW::PixelFormat::PF4:
					IW::Convert4to32(pLineIn, pLineSrc, 0, cxIn, _palette);
					break;
				case IW::PixelFormat::PF8:
					IW::Convert8to32(pLineIn, pLineSrc, 0, cxIn, _palette);
					break;
				case IW::PixelFormat::PF8Alpha:
					IW::Convert8Alphato32(pLineIn, pLineSrc, 0, cxIn, _palette);
					break;
				case IW::PixelFormat::PF8GrayScale:
					IW::Convert8GrayScaleto32(pLineIn, pLineSrc, 0, cxIn);
					break;
				case IW::PixelFormat::PF555:
					IW::Convert555to32(pLineIn, pLineSrc, 0, cxIn);
					break;
				case IW::PixelFormat::PF565:
					IW::Convert565to32(pLineIn, pLineSrc, 0, cxIn);
					break;
				case IW::PixelFormat::PF24:
					IW::Convert24to32(pLineIn, pLineSrc, 0, cxIn);
					break;
				case IW::PixelFormat::PF32:
				case IW::PixelFormat::PF32Alpha:
					pLineIn = static_cast<LPDWORD>(const_cast<LPVOID>(pBitsIn));
					break;
				}

				int yyy = (y * cyOut);
				int yOut = yyy / cyIn; // Round down!!
				bool firstRow = (yyy % cyIn) == 0;
				
				assert(cyOut > yOut); // overflow?
				IW::LPRGBSUM pSum = _pRGBSum + (cxOut * yOut);
				IW::ToSums(pSum, pLineIn, cxOut, cxIn, _pf.HasAlpha() || isHighQuality, firstRow);

				_nLastYIn = yOut;
				int nLineLimit = _nLastYIn - 1;

				if (_bIsSequential && (_nLastYOut < nLineLimit))
				{
					MoveSumToPage(nLineLimit);
				}
			}
		}

		void SetPalette(IW::LPCCOLORREF pPalette)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetPalette(pPalette);
			}
			else
			{
				IW::MemCopy(_palette, pPalette, sizeof(COLORREF) * _pf.NumberOfPaletteEntries());
			}
		}

		// Attributes
		CSize GetThumbnailSize()
		{
			return _sizeThumbnail;
		}

		bool WantBitmap()
		{
			return _search.AbortMatch(_ft, _strTitle, _strTags, _strDescription);
		}

		bool WantRawImage()
		{
			return false;
		}

		// Image Attributes
		void SetImageFlags(const DWORD dw)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetImageFlags(dw);
			}
			else
			{
				_dwImageFlags |= dw;	
			}
		}

		void SetTimeDelay(DWORD dw)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetTimeDelay(dw);
			}
			else
			{
				_dwPageFlags |= IW::PageFlags::HasTimeDelay;
			}
		}

		void SetDisposalMethod(DWORD dw)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetDisposalMethod(dw);
			}
		}

		void SetBackGround(DWORD dw)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetBackGround(dw);
			}
			else
			{
				_dwPageFlags |= IW::PageFlags::HasBackGround;
			}
		}


		void SetTransparent(DWORD dw)
		{
			if (!_bNeedsThumbnailing)
			{
				_imageStream.SetTransparent(dw);
			}
			else
			{
				_dwPageFlags |= IW::PageFlags::HasTransparent;
			}
		}

		void SetCameraSettings(const CameraSettings &settings)
		{
			_ft = settings.DateTaken;
			_imageStream.SetCameraSettings(settings);
		}

		// Meta Data Attributes
		void SetStatistics(const CString &str)
		{
			_imageStream.SetStatistics(str);
		}

		void SetLoaderName(const CString &str)
		{
			_imageStream.SetLoaderName(str);
		}

		void AddImageData(IW::IStreamIn *pStreamIn, DWORD dwType)
		{
			// Do nothing for thumbnail
		}

		void AddMetaDataBlob(IW::MetaData &iptc, IW::MetaData &xmp)
		{
			ImageMetaData meta(iptc, xmp);
			_strTitle = meta.GetTitle();
			_strTags = meta.GetTags();
			_strDescription = meta.GetDescription();

			_imageStream.SetTitle(_strTitle);
			_imageStream.SetTags(_strTags);
			_imageStream.SetDescription(_strDescription);
			_imageStream.SetObjectName(meta.GetObjectName());

			AddBlob(iptc);
			AddBlob(xmp);
		}

		void AddBlob(IW::MetaData &blob)
		{
			if (!blob.IsEmpty())
			{
				if (blob.IsType(IW::MetaDataTypes::PROFILE_EXIF)) SetImageFlags(IW::ImageFlags::HasExif);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_IPTC)) SetImageFlags(IW::ImageFlags::HasIPTC);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_XMP)) SetImageFlags(IW::ImageFlags::HasXmp);
				if (blob.IsType(IW::MetaDataTypes::PROFILE_ICC)) SetImageFlags(IW::ImageFlags::HasICC);
			}
		}
	};

	template<class TBase>
	class ImageStreamScale : public ImageStreamThumbnail<TBase, true>
	{
	public:
		ImageStreamScale(IW::Image &image, const Search::Spec &search, const CSize &sizeThumbnail  = App.Settings._sizeThumbImage) :  ImageStreamThumbnail<TBase, true>(image, search, sizeThumbnail)
		{
		}
	};
}
