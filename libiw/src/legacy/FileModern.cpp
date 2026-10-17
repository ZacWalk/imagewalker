#include <libiw/Codec.h>
#include <libiw/WebPCodec.h>
#include <libiw/Diagnostics.h>
#include "../NativeCodec.h"
#include "LegacyPch.h"
#include "FileModern.h"

namespace
{
	bool read_modern(const iw::imaging::PixelDecoder decoder, IW::IStreamIn* input, IW::IImageStream* output,
	                 IW::IStatus* status, const CString& key, const CString& title)
	{
		const int size = input->GetFileSize();
		if (size <= 0 || size > 512 * 1024 * 1024)
		{
			status->SetError(L"The encoded image is empty or exceeds the input limit.");
			return false;
		}
		input->Seek(IW::IStreamCommon::eBegin, 0);
		DWORD read{};
		if (decoder == iw::imaging::PixelDecoder::webp)
		{
			// The loader is also reached by a RIFF header word, which AVI and WAV files share.
			std::uint8_t header[12]{};
			if (!input->Read(header, sizeof(header), &read) || !iw::imaging::has_webp_signature({header, read}))
			{
				status->SetError(L"The file does not carry a WebP signature.");
				return false;
			}
			input->Seek(IW::IStreamCommon::eBegin, 0);
		}
		std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
		if (!input->Read(bytes.data(), static_cast<DWORD>(bytes.size()), &read) || read != bytes.size())
		{
			status->SetError(L"The encoded image could not be read completely.");
			return false;
		}
		iw::imaging::DecodeOptions options;
		const auto thumbnail = output->GetThumbnailSize();
		options.maximumWidth = thumbnail.cx;
		options.maximumHeight = thumbnail.cy;
		options.thumbnail = thumbnail.cx > 0 && thumbnail.cy > 0;
		options.applyOrientation = output->WantUpright();
		options.cancelled = [status] { return status->QueryCancel(); };
		options.progress = [status](int current, int total) { status->Progress(current, total); };
		auto decoded = iw::imaging::decode_with(decoder, bytes, {}, options);
		if (!decoded)
		{
			status->SetError(decoded.message.c_str());
			return false;
		}
		const auto& image = decoded.image;
		IW::CameraSettings settings;
		settings.OriginalImageSize = CSize(image.originalWidth, image.originalHeight);
		settings.OriginalBpp = IW::PixelFormat::PF32Alpha;
		settings.Orientation = decoded.orientation;
		output->SetCameraSettings(settings);
		output->SetLoaderName(key);
		output->SetStatistics(title);
		output->CreatePage(CRect(0, 0, image.width, image.height), IW::PixelFormat::PF32Alpha, true);
		for (int y = 0; y < image.height; ++y)
		{
			if (status->QueryCancel())
			{
				status->SetError(L"Image decoding was cancelled.");
				return false;
			}
			output->SetBitmap(y, image.pixels.data() + static_cast<size_t>(y) * image.width);
		}
		output->Flush();
		return true;
	}
}

bool CLoadWebP::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return read_modern(iw::imaging::PixelDecoder::webp, input, output, status, GetKey(), GetTitle());
}

bool CLoadWebP::Write(const CString&, IW::IStreamOut* output, const IW::Image& image,
                     const IW::CodecSettings& settings, IW::IStatus* status)
{
	if (image.GetPageCount() != 1)
	{
		status->SetError(L"Static WebP output requires exactly one image frame.");
		return false;
	}
	if (status->QueryCancel())
	{
		status->SetError(L"Image encoding was cancelled.");
		return false;
	}
	iw::imaging::SaveOptions options;
	options.quality = settings.JpegQuality;
	const auto encoded = iw::webp::encode(iw::imaging::image_pixels(image), options);
	if (encoded.empty() || encoded.size() > MAXDWORD)
	{
		status->SetError(L"The image could not be encoded as WebP.");
		return false;
	}
	if (!image.Blobs.empty())
	{
		constexpr auto warning = L"WebP output does not carry source metadata.";
		status->SetWarning(warning);
		iw::imaging::report_diagnostic(warning);
	}
	DWORD written{};
	if (!output->Write(encoded.data(), static_cast<DWORD>(encoded.size()), &written) || written != encoded.size())
	{
		status->SetError(L"The WebP output could not be written completely.");
		return false;
	}
	return true;
}

CString CLoadWindows::_GetExtensionList()
{
	CString result;
	for (const auto& extension : iw::imaging::windows_image_extensions())
	{
		if (extension.size() < 2 || extension.front() != L'.') continue;
		if (!result.IsEmpty()) result += L",";
		result += extension.c_str() + 1;
	}
	return result;
}

bool CLoadWindows::Read(const CString&, IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
{
	return read_modern(iw::imaging::PixelDecoder::wic, input, output, status, GetKey(), GetTitle());
}
