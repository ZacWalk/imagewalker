#pragma once
#include "ViewModelItems.h"
#include "ImagingStreams.h"
#include "iw/imaging/FileFormatAny.h"

class CLoadAny : public CLoadAnyCore
{
public:
	explicit CLoadAny(ImageLoaders& loaders) : CLoadAnyCore(loaders) {}
	using CLoadAnyCore::Read;

	bool Read(const CString& type, IW::IStreamIn* stream, IW::Image& image, IW::IStatus* status)
	{
		IW::ImageStream<IW::IImageStream> output(image);
		return CLoadAnyCore::Read(type, stream, &output, status);
	}

	bool LoadImage(const CString& path, IW::Image& image, IW::IStatus* status)
	{
		IW::ImageStream<IW::IImageStream> output(image);
		return LoadImage(path, &output, status);
	}
	bool LoadImage(const CString& path, IW::IImageStream* output, IW::IStatus* status);
	bool SaveImage(const CString& path, IW::Image& image, const IW::CodecSettings& settings, IW::IStatus* status);
	HWND CreateSettingsWindow(const CString& type, HWND parent, IW::CodecSettings& settings);
};
