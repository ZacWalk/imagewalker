#pragma once
#include "iw/imaging/ImagingStreams.h"
#include "ViewModelSearch.h"

namespace IW
{
	template<class TBase, bool wantRaw = true>
	class ImageStream : public ImageStreamCore<TBase, wantRaw>
	{
	public:
		explicit ImageStream(Image& image) : ImageStreamCore<TBase, wantRaw>(image, App.Settings._bExifAutoRotate) {}
	};

	template<class TBase, bool highQuality = false>
	class ImageStreamThumbnail : public ImageStreamThumbnailCore<TBase, highQuality>
	{
	public:
		ImageStreamThumbnail(Image& image, const Search::Spec& search, const CSize& size = App.Settings._sizeThumbImage)
			: ImageStreamThumbnailCore<TBase, highQuality>(image, search, size, App.Settings._bExifAutoRotate) {}
	};

	template<class TBase>
	using ImageStreamScale = ImageStreamThumbnail<TBase, true>;
}
