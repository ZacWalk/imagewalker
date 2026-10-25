#pragma once
#include "FileFormat.h"

class CLoadWebP : public CLoad<CLoadWebP>
{
public:
	static CString _GetKey() { return L"WebP"; }
	static CString _GetTitle() { return L"WebP"; }
	static CString _GetDescription() { return L"Static WebP image"; }
	static CString _GetExtensionList() { return L"WEBP"; }
	static CString _GetExtensionDefault() { return L"WEBP"; }
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::SAVE | IW::ImageLoaderFlags::ALPHA; }
	bool Read(const CString& type, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status) override;
	bool Write(const CString& type, IW::IStreamOut* output, const IW::Image& image,
	           const IW::CodecSettings& settings, IW::IStatus* status) override;
};

class CLoadWindows : public CLoad<CLoadWindows>
{
public:
	static CString _GetKey() { return L"WindowsImagingComponent"; }
	static CString _GetTitle() { return L"Windows image codecs"; }
	static CString _GetDescription() { return L"Additional installed Windows image codecs"; }
	static CString _GetExtensionList();
	static CString _GetExtensionDefault() { return L"WIC"; }
	static DWORD _GetFlags() { return IW::ImageLoaderFlags::ALPHA; }
	bool Read(const CString& type, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status) override;
	bool Write(const CString&, IW::IStreamOut*, const IW::Image&, const IW::CodecSettings&, IW::IStatus* status) override
	{
		status->SetError(L"This Windows image format is read-only; choose a supported output format.");
		return false;
	}
};
