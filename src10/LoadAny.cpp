#include "stdafx.h"
#include "LoadAny.h"
#include "dibthumb.h"
#include "iw/logfile.h"
#include <libiw/Document.h>

namespace
{
	iw::imaging::DecodeOptions decode_options(CStatus* status, bool thumbnail)
	{
		iw::imaging::DecodeOptions options;
		options.thumbnail = thumbnail;
		// ArtMate shows pixels as stored and is bounded by the 1 GiB native page ceiling.
		options.applyOrientation = false;
		options.maximumBytes = std::uint64_t{1} << 30;
		if (thumbnail)
		{
			options.maximumWidth = IMAGE_X;
			options.maximumHeight = IMAGE_Y;
		}
		if (status)
		{
			options.cancelled = [status] { return status->Exit() != FALSE; };
			options.progress = [status](int current, int total) { status->Status(current, total); };
		}
		return options;
	}

	bool import_image(CDib& dib, const iw::imaging::Document& image)
	{
		const auto frame = image.frame(0);
		using Format = iw::imaging::PixelStorage;
		int bits = 32;
		switch (frame.format)
		{
		case Format::indexed1: bits = 1; break;
		case Format::indexed4: bits = 4; break;
		case Format::indexed8:
		case Format::indexed8Alpha:
		case Format::gray8: bits = 8; break;
		case Format::bgr24: bits = image.format_key() == L"TaggedImageFile" ? 32 : 24; break;
		default: break;
		}
		if (!dib.Create(frame.width, frame.height, bits, FALSE))
		{
			IW::Logging::Error(_T("Unable to allocate the decoded DIB"));
			return false;
		}
		if (bits == 32)
		{
			image.copy_bgra({reinterpret_cast<std::uint32_t*>(dib.GetBitmap()),
				static_cast<size_t>(dib.m_nStorageWidth / 4) * frame.height}, dib.m_nStorageWidth / 4, true);
		}
		else
		{
			for (int y = 0; y < frame.height; ++y)
				memcpy(dib.GetBitmap(y), frame.pixels.data() +
					static_cast<size_t>(frame.bottomUp ? frame.height - 1 - y : y) * frame.stride, dib.m_nStorageWidth);
			if (frame.format == Format::gray8)
			{
				for (unsigned i = 0; i < 256; ++i)
					dib.GetColor()[i] = {static_cast<BYTE>(i), static_cast<BYTE>(i), static_cast<BYTE>(i), 255};
			}
			else if (bits <= 8)
			{
				const auto count = (std::min)(size_t{1} << bits, frame.paletteBgra.size() / 4);
				memcpy(dib.GetColor(), frame.paletteBgra.data(), count * sizeof(RGBQUAD));
				if ((frame.flags & 0x04) && frame.transparent < count)
					dib.GetColor()[frame.transparent].rgbReserved = 0;
			}
		}
		dib.m_sizeOriginal = CSize(image.original_width() > 0 ? image.original_width() : frame.width,
			image.original_height() > 0 ? image.original_height() : frame.height);
		dib.m_strInfo = CW2A(image.description().c_str());
		return true;
	}

	BOOL finish_decode(CDib& dib, const iw::imaging::DocumentResult& result)
	{
		for (const auto& warning : result.warnings) IW::Logging::Warn(_T("Image decode: %ls"), warning.c_str());
		if (!result)
		{
			// Every file in a folder is offered for decoding; only real failures are logged.
			if (result.error != iw::imaging::ImageError::cancelled &&
				result.error != iw::imaging::ImageError::unsupported)
				IW::Logging::Warn(_T("Image decode: %ls"), result.message.c_str());
			return FALSE;
		}
		return import_image(dib, result.document) ? TRUE : FALSE;
	}
}

BOOL CLoadAny::Load(CDib* dib, LPBYTE bytes, DWORD size, CStatus* status, BOOL thumbnail)
{
	if (!dib || (!bytes && size != 0))
	{
		IW::Logging::Error(_T("Invalid image decode destination or buffer"));
		return FALSE;
	}
	dib->Close();
	try
	{
		return finish_decode(*dib, iw::imaging::decode_document(
			std::span<const std::uint8_t>(bytes, size), {}, decode_options(status, thumbnail != FALSE)));
	}
	catch (const std::bad_alloc&)
	{
		dib->Close();
		IW::Logging::Error(_T("Not enough memory to load the image"));
		return FALSE;
	}
	catch (const std::invalid_argument& error)
	{
		dib->Close();
		IW::Logging::Error(_T("Invalid decoded image: %hs"), error.what());
		return FALSE;
	}
}

BOOL CLoadAny::Load(CDib* dib, LPCSTR path, CStatus* status, BOOL thumbnail)
{
	if (!dib || !path || !*path)
	{
		IW::Logging::Error(_T("Invalid image decode destination or path"));
		return FALSE;
	}
	dib->Close();
	const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
	if (length <= 1)
	{
		IW::Logging::LastError(_T("Convert image path to Unicode"));
		return FALSE;
	}
	try
	{
		std::wstring wide(static_cast<size_t>(length), L'\0');
		if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, wide.data(), length))
		{
			IW::Logging::LastError(_T("Convert image path to Unicode"));
			return FALSE;
		}
		wide.pop_back();
		return finish_decode(*dib, iw::imaging::decode_document(
			std::filesystem::path(wide), decode_options(status, thumbnail != FALSE)));
	}
	catch (const std::bad_alloc&)
	{
		dib->Close();
		IW::Logging::Error(_T("Not enough memory to load the image"));
		return FALSE;
	}
	catch (const std::invalid_argument& error)
	{
		dib->Close();
		IW::Logging::Error(_T("Invalid decoded image: %hs"), error.what());
		return FALSE;
	}
}

BOOL CLoadAny::Load(CDib* dib, int resource, CStatus* status, BOOL thumbnail)
{
	const HINSTANCE instance = _AtlBaseModule.GetResourceInstance();
	const HRSRC found = FindResource(instance, MAKEINTRESOURCE(resource), "image");
	const HGLOBAL loaded = found ? LoadResource(instance, found) : nullptr;
	const DWORD size = found ? SizeofResource(instance, found) : 0;
	auto* bytes = loaded ? static_cast<BYTE*>(LockResource(loaded)) : nullptr;
	if (!bytes || !size)
	{
		IW::Logging::Error(_T("Image resource %d could not be loaded"), resource);
		return FALSE;
	}
	return Load(dib, bytes, size, status, thumbnail);
}
