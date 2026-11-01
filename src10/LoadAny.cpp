#include "stdafx.h"
#include "LoadAny.h"
#include "dibthumb.h"
#include "iw/logfile.h"
#include <iw/imaging/Document.h>

namespace
{
	iw::imaging::DecodeOptions decode_options(CStatus* status, bool thumbnail)
	{
		iw::imaging::DecodeOptions options;
		options.thumbnail = thumbnail;
		// ImageWalker 1.0 shows stored pixels within the 1 GiB native page ceiling.
		options.applyOrientation = false;
		options.legacyTiffStorage = true;
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

	bool import_image(CDib& dib, const iw::imaging::DocumentResult& image)
	{
		const auto& frame = image.document.frames.front();
		using Format = libfiles::document_storage;
		const bool indexed_storage = frame.storage == Format::indexed1 ||
			frame.storage == Format::indexed2 || frame.storage == Format::indexed4 ||
			frame.storage == Format::indexed8 || frame.storage == Format::indexed8_alpha;
		const bool indexed_format = image.formatKey == L"GIF" ||
			image.formatKey == L"PCX" ||
			(image.formatKey == L"BMP" &&
			(indexed_storage ||
				(image.document.info.bits_per_pixel > 0 &&
				 image.document.info.bits_per_pixel <= 8)));
		if (indexed_format)
		{
			const auto width = static_cast<std::size_t>(frame.width);
			const auto height = static_cast<std::size_t>(frame.height);
			std::vector<std::uint32_t> pixels(width * height);
			try
			{
				libfiles::copy_bgra(frame.rows(), pixels, width, false);
			}
			catch (const std::exception&)
			{
				IW::Logging::Error(_T("Unable to convert indexed legacy pixels"));
				return false;
			}
			std::vector<std::uint32_t> palette;
			palette.reserve(256);
			std::vector<std::uint8_t> indices(pixels.size());
			for (std::size_t i = 0; i < pixels.size(); ++i)
			{
				auto found = std::ranges::find(palette, pixels[i]);
				if (found == palette.end())
				{
					if (palette.size() == 256) return false;
					palette.push_back(pixels[i]);
					found = palette.end() - 1;
				}
				indices[i] = static_cast<std::uint8_t>(found - palette.begin());
			}
			if (!dib.Create(static_cast<int>(frame.width), static_cast<int>(frame.height),
				8, FALSE))
				return false;
			for (std::size_t i = 0; i < palette.size(); ++i)
				reinterpret_cast<RGBQUAD*>(dib.GetColor())[i] = {
					static_cast<BYTE>(palette[i]),
					static_cast<BYTE>(palette[i] >> 8),
					static_cast<BYTE>(palette[i] >> 16),
					static_cast<BYTE>(palette[i] >> 24)};
			for (std::size_t y = 0; y < height; ++y)
				memcpy(dib.GetBitmap(static_cast<int>(y)), indices.data() + y * width, width);
			dib.m_sizeOriginal = CSize(image.originalWidth > 0 ? image.originalWidth : frame.width,
				image.originalHeight > 0 ? image.originalHeight : frame.height);
			dib.m_strInfo = CW2A(image.description.c_str());
			return true;
		}
		if (image.formatKey == L"JPEG-CMYK" && frame.width > IMAGE_X)
		{
			const auto source_width = static_cast<std::size_t>(frame.width);
			const auto source_height = static_cast<std::size_t>(frame.height);
			std::vector<std::uint32_t> source(source_width * source_height);
			libfiles::copy_bgra(frame.rows(), source, source_width, false);
			const auto scale = (std::min)(
				static_cast<double>(IMAGE_X) / frame.width,
				static_cast<double>(IMAGE_Y) / frame.height);
			const auto width = static_cast<int>((std::max)(1.0, frame.width * scale));
			const auto height = static_cast<int>((std::max)(1.0, frame.height * scale));
			if (!dib.Create(width, height, 32, FALSE)) return false;
			for (int y = 0; y < height; ++y)
			{
				auto* destination = reinterpret_cast<std::uint32_t*>(dib.GetBitmap(y));
				const auto source_y = static_cast<std::size_t>(
					static_cast<std::uint64_t>(y) * source_height / height);
				for (int x = 0; x < width; ++x)
				{
					const auto source_x = static_cast<std::size_t>(
						static_cast<std::uint64_t>(x) * source_width / width);
					destination[x] = source[source_y * source_width + source_x];
				}
			}
			dib.m_sizeOriginal = CSize(image.originalWidth > 0 ? image.originalWidth : frame.width,
				image.originalHeight > 0 ? image.originalHeight : frame.height);
			dib.m_strInfo = CW2A(image.description.c_str());
			return true;
		}
		int bits = 32;
		switch (frame.storage)
		{
		case Format::indexed1: bits = 1; break;
		case Format::indexed4: bits = 4; break;
		case Format::indexed8:
		case Format::indexed8_alpha:
		case Format::gray8: bits = 8; break;
		case Format::bgr24:
			bits = image.formatKey == L"TIFF" ||
				image.formatKey == L"TaggedImageFile" ||
				(image.formatKey == L"JPEG-CMYK" && frame.width > IMAGE_X) ? 32 : 24;
			break;
		case Format::bgrx32:
			bits = image.formatKey == L"BMP" &&
				!(image.originalWidth > static_cast<int>(frame.width) ||
				  image.originalHeight > static_cast<int>(frame.height)) ? 24 : 32;
			break;
		case Format::bgra32:
			bits = image.formatKey == L"BMP" &&
				!(image.originalWidth > static_cast<int>(frame.width) ||
				  image.originalHeight > static_cast<int>(frame.height)) ? 24 : 32;
			break;
		default: break;
		}
		const int width = static_cast<int>(frame.width);
		const int height = static_cast<int>(frame.height);
		if (!dib.Create(width, height, bits, FALSE))
		{
			IW::Logging::Error(_T("Unable to allocate the decoded DIB"));
			return false;
		}
		if (bits == 32)
		{
			libfiles::copy_bgra(frame.rows(), {reinterpret_cast<std::uint32_t*>(dib.GetBitmap()),
				static_cast<size_t>(dib.m_nStorageWidth / 4) * height}, dib.m_nStorageWidth / 4, true);
		}
		else
		{
			if (bits == 24 && (frame.storage == Format::bgrx32 ||
				frame.storage == Format::bgra32))
			{
				std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
				libfiles::copy_bgra(frame.rows(), pixels, width, false);
				for (int y = 0; y < height; ++y)
				{
					auto* destination = dib.GetBitmap(y);
					for (int x = 0; x < width; ++x)
					{
						const auto pixel = pixels[static_cast<std::size_t>(y) * width + x];
						destination[x * 3] = static_cast<BYTE>(pixel);
						destination[x * 3 + 1] = static_cast<BYTE>(pixel >> 8);
						destination[x * 3 + 2] = static_cast<BYTE>(pixel >> 16);
					}
				}
			}
			else
				for (int y = 0; y < height; ++y)
					memcpy(dib.GetBitmap(y), frame.pixels.data() +
						static_cast<size_t>(y) * frame.stride, dib.m_nStorageWidth);
			if (frame.storage == Format::gray8)
			{
				for (unsigned i = 0; i < 256; ++i)
					dib.GetColor()[i] = {static_cast<BYTE>(i), static_cast<BYTE>(i), static_cast<BYTE>(i), 255};
			}
			else if (bits <= 8)
			{
				const auto count = (std::min)(size_t{1} << bits, frame.palette.size());
				memcpy(dib.GetColor(), frame.palette.data(), count * sizeof(RGBQUAD));
				if (frame.transparent && *frame.transparent < count)
					dib.GetColor()[*frame.transparent].rgbReserved = 0;
			}
		}
		dib.m_sizeOriginal = CSize(image.originalWidth > 0 ? image.originalWidth : width,
			image.originalHeight > 0 ? image.originalHeight : height);
		dib.m_strInfo = CW2A(image.description.c_str());
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
		return import_image(dib, result) ? TRUE : FALSE;
	}

	bool looks_like_pcx(const std::span<const std::uint8_t> bytes)
	{
		return bytes.size() >= 128 && bytes[0] == 0x0a && bytes[1] <= 5 &&
			bytes[2] == 1 && bytes[3] == 8 && bytes[8] <= 0xff &&
			bytes[9] <= 0xff;
	}

	std::wstring extension_from_signature(const std::span<const std::uint8_t> bytes)
	{
		if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return L".bmp";
		if (bytes.size() >= 6 && bytes[0] == 'G' && bytes[1] == 'I' &&
			bytes[2] == 'F') return L".gif";
		if (bytes.size() >= 4 &&
			((bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42 && bytes[3] == 0) ||
			 (bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 0 && bytes[3] == 42)))
			return L".tif";
		if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8) return L".jpg";
		if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
			bytes[2] == 'N' && bytes[3] == 'G') return L".png";
		if (looks_like_pcx(bytes)) return L".pcx";
		return {};
	}

	bool looks_like_adobe_cmyk(const std::span<const std::uint8_t> bytes)
	{
		for (std::size_t i = 0; i + 12 < bytes.size(); ++i)
			if (bytes[i] == 0xff && bytes[i + 1] == 0xee &&
				std::memcmp(bytes.data() + i + 4, "Adobe", 5) == 0)
				return true;
		return false;
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
		const auto input = std::span<const std::uint8_t>(bytes, size);
		const auto extension = extension_from_signature(input);
		auto result = iw::imaging::decode_document(input, extension,
			decode_options(status, thumbnail != FALSE));
		if (result && extension == L".bmp") result.formatKey = L"BMP";
		else if (result && extension == L".gif") result.formatKey = L"GIF";
		else if (result && extension == L".pcx") result.formatKey = L"PCX";
		else if (result && extension == L".tif") result.formatKey = L"TIFF";
		else if (result && extension == L".jpg")
			result.formatKey = looks_like_adobe_cmyk(input) ? L"JPEG-CMYK" : L"JPEG";
		return finish_decode(*dib, result);
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
