#include "stdafx.h"
#include "FileFormatAny.h"
#include "iw/imagingbitmap.h"

using namespace IW;

bool Image::Copy(HDC, HBITMAP bitmap)
{
	return CopyRenderedBitmap(*this, bitmap);
}

Image Image::LoadPreviewImage(ImageLoaders& loaders)
{
	Image image;
	CLoadAny loader(loaders);
	StreamResource f(App.GetResourceInstance(), IDR_PREVIEW_SAMPLE);
	loader.Read(_T("JPG"), &f, image, CNullStatus::Instance);
	return image;
}
