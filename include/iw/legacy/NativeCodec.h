#pragma once

#include <iw/imaging/Document.h>
#include <iw/legacy/LegacyPch.h>

#include <libfiles/encode.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <vector>

// The historical IW::Image boundary is deliberately kept out of the modern
// codec headers. This bridge owns the storage translation and native encoder
// policy used by the legacy stream adapters.
namespace iw::imaging::legacy_detail
{
	inline bool metadata_is_photoshop(const IW::MetaData& blob)
	{
		return !blob.IsEmpty() && blob.GetDataSize() >= 14 &&
			std::memcmp(blob.GetData(), "Photoshop 3.0\0", 14) == 0;
	}

	inline std::optional<libfiles::document_metadata> metadata_kind(const IW::MetaData& blob)
	{
		if (blob.IsImageData()) return {};
		switch (blob.GetType())
		{
		case IW::MetaDataTypes::PROFILE_EXIF: return libfiles::document_metadata::exif;
		case IW::MetaDataTypes::PROFILE_IPTC:
			return metadata_is_photoshop(blob) ? libfiles::document_metadata::photoshop :
				libfiles::document_metadata::iptc;
		case IW::MetaDataTypes::PROFILE_XMP: return libfiles::document_metadata::xmp;
		case IW::MetaDataTypes::PROFILE_ICC: return libfiles::document_metadata::icc;
		case IW::MetaDataTypes::PROFILE_SOURCE_ICC: return libfiles::document_metadata::source_icc;
		case IW::MetaDataTypes::PROFILE_COMMENT: return libfiles::document_metadata::comment;
		case IW::MetaDataTypes::PROFILE_AUTHOR: return libfiles::document_metadata::author;
		case IW::MetaDataTypes::PROFILE_PNG_TEXT: return libfiles::document_metadata::png_text;
		case IW::MetaDataTypes::PROFILE_PHOTOSHOP: return libfiles::document_metadata::photoshop;
		default: return {};
		}
	}

	inline libfiles::pixel_rows page_rows(const IW::Page& page)
	{
		const auto format = page.GetPixelFormat();
		libfiles::pixel_rows rows;
		rows.width = static_cast<std::uint32_t>(page.GetWidth());
		rows.height = static_cast<std::uint32_t>(page.GetHeight());
		rows.storage = static_cast<libfiles::document_storage>(format._pf);
		rows.stride = static_cast<std::size_t>(page.GetStorageWidth());
		rows.bottom_up = true;
		rows.pixels = {page.GetBitmap(), rows.stride * rows.height};
		rows.palette = {reinterpret_cast<const std::uint32_t*>(page.GetPalette()),
			static_cast<std::size_t>(format.NumberOfPaletteEntries())};
		if (page.GetFlags() & IW::PageFlags::HasTransparent)
			rows.transparent = static_cast<std::uint32_t>(page.GetTransparent());
		return rows;
	}

	inline libfiles::decoded_frame decoded_page(const IW::Page& page)
	{
		const auto rows = page_rows(page);
		const auto rect = page.GetPageRect();
		libfiles::decoded_frame frame;
		frame.left = rect.left;
		frame.top = rect.top;
		frame.width = rows.width;
		frame.height = rows.height;
		frame.storage = rows.storage;
		frame.stride = rows.stride;
		frame.pixels.resize(rows.pixels.size());
		for (std::size_t y = 0; y < rows.height; ++y)
			std::memcpy(frame.pixels.data() + y * rows.stride,
				rows.pixels.data() + (rows.height - 1 - y) * rows.stride, rows.stride);
		frame.palette.assign(rows.palette.begin(), rows.palette.end());
		frame.transparent = rows.transparent;
		const auto flags = page.GetFlags();
		if (flags & IW::PageFlags::HasBackGround) frame.background = page.GetBackGround();
		if (flags & IW::PageFlags::HasTimeDelay) frame.delay_ms = page.GetTimeDelay();
		if (const auto disposal = page.GetDisposalMethod(); disposal != 0)
			frame.disposal = disposal;
		return frame;
	}

	inline void to_document(const IW::Image& image, DocumentResult& result)
	{
		auto& document = result.document;
		for (const auto& page : image.Pages) document.frames.push_back(decoded_page(page));
		for (const auto& blob : image.Blobs)
		{
			const auto kind = metadata_kind(blob);
			if (!kind) continue;
			const auto* bytes = blob.IsEmpty() ? nullptr : blob.GetData();
			document.metadata.push_back({*kind, bytes
				? std::vector<std::uint8_t>(bytes, bytes + blob.GetDataSize())
				: std::vector<std::uint8_t>{}});
		}
		if (!document.frames.empty())
		{
			const auto& first = document.frames.front();
			document.info.width = first.width;
			document.info.height = first.height;
			document.info.storage = first.storage;
			document.info.bits_per_pixel = libfiles::storage_bits(first.storage);
		}
		document.info.frames = static_cast<std::uint32_t>(document.frames.size());
		const auto& camera = image.GetCameraSettings();
		document.info.has_resolution = camera.XPelsPerMeter != 0 || camera.YPelsPerMeter != 0;
		document.info.pixels_per_meter_x = camera.XPelsPerMeter;
		document.info.pixels_per_meter_y = camera.YPelsPerMeter;
		result.orientation = camera.Orientation;
		result.originalWidth = camera.OriginalImageSize.cx;
		result.originalHeight = camera.OriginalImageSize.cy;
		result.description = image.GetStatistics().GetString();
		result.formatKey = image.GetLoaderName().GetString();
	}

	inline bool write_stream_data(IW::IStreamOut* output,
		const std::vector<std::uint8_t>& data)
	{
		if (!output) return false;
		const auto first = (std::max)(std::size_t{1}, data.size() / 2);
		for (std::size_t offset = 0; offset < data.size();)
		{
			const auto count = offset == 0 ? first : data.size() - offset;
			DWORD written{};
			if (count > MAXDWORD ||
				!output->Write(data.data() + offset, static_cast<DWORD>(count), &written) ||
				written != count)
				return false;
			offset += count;
		}
		return true;
	}
}

namespace iw::imaging
{
	inline PixelImage image_pixels(const IW::Image& image, const std::size_t index = 0)
	{
		if (index >= image.Pages.size()) throw std::out_of_range("Image frame index.");
		const auto& page = image.Pages[index];
		PixelImage result;
		result.width = page.GetWidth();
		result.height = page.GetHeight();
		const auto original = image.GetCameraSettings().OriginalImageSize;
		result.originalWidth = original.cx > 0 ? original.cx : result.width;
		result.originalHeight = original.cy > 0 ? original.cy : result.height;
		result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
		libfiles::copy_bgra(legacy_detail::page_rows(page), result.pixels,
			static_cast<std::size_t>(result.width), false);
		return result;
	}

	inline libfiles::pixel_rows image_rows(const IW::Image& image, const std::size_t index = 0)
	{
		if (index >= image.Pages.size()) throw std::out_of_range("Image frame index.");
		return legacy_detail::page_rows(image.Pages[index]);
	}

	inline bool write_libfiles_document(libfiles::image_format format, IW::IStreamOut* output,
		const IW::Image& image, const IW::CodecSettings& settings, IW::IStatus* status)
	{
		if (!output || !status || image.IsEmpty())
		{
			if (status) status->SetError(L"The image has no complete frame.");
			return false;
		}
		if (!libfiles::can_encode_document(format))
		{
			status->SetError(L"This native format is not available in libfiles.");
			return false;
		}
		DocumentResult converted;
		try
		{
			legacy_detail::to_document(image, converted);
		}
		catch (const std::bad_alloc&)
		{
			status->SetError(L"Not enough memory to prepare the image.");
			return false;
		}
		if (converted.document.frames.empty())
		{
			status->SetError(L"The image has no complete frame.");
			return false;
		}
		if (format == libfiles::image_format::jpeg)
		{
			const auto storage = converted.document.frames.front().storage;
			if (storage != libfiles::document_storage::gray8 &&
				storage != libfiles::document_storage::bgr24 &&
				storage != libfiles::document_storage::bgrx32 &&
				storage != libfiles::document_storage::bgra32)
			{
				if (image.GetPageCount() != 1)
				{
					status->SetError(L"JPEG output requires one image frame.");
					return false;
				}
				const auto pixels = image_pixels(image);
				auto& frame = converted.document.frames.front();
				frame.left = 0;
				frame.top = 0;
				frame.width = static_cast<std::uint32_t>(pixels.width);
				frame.height = static_cast<std::uint32_t>(pixels.height);
				frame.storage = libfiles::document_storage::bgra32;
				frame.stride = static_cast<std::size_t>(pixels.width) * sizeof(std::uint32_t);
				frame.pixels.resize(frame.stride * frame.height);
				std::memcpy(frame.pixels.data(), pixels.pixels.data(), frame.pixels.size());
				frame.palette.clear();
				converted.document.info.width = frame.width;
				converted.document.info.height = frame.height;
				converted.document.info.storage = frame.storage;
				converted.document.info.bits_per_pixel = libfiles::storage_bits(frame.storage);
			}
		}
		if (format == libfiles::image_format::gif)
		{
			for (auto& frame : converted.document.frames)
			{
				const auto transparent = frame.transparent;
				for (std::size_t i = 0; i < frame.palette.size(); ++i)
					frame.palette[i] = (frame.palette[i] & 0x00ffffffu) |
						(transparent && *transparent == i ? 0u : 0xff000000u);
			}
		}
		if (format == libfiles::image_format::bmp ||
			format == libfiles::image_format::tiff)
		{
			for (auto& frame : converted.document.frames)
			{
				if (frame.storage != libfiles::document_storage::gray8) continue;
				frame.storage = libfiles::document_storage::indexed8;
				frame.palette.resize(256);
				for (std::size_t i = 0; i < frame.palette.size(); ++i)
					frame.palette[i] = 0xff000000u | static_cast<std::uint32_t>(i) * 0x010101u;
				converted.document.info.storage = frame.storage;
				converted.document.info.bits_per_pixel = libfiles::storage_bits(frame.storage);
			}
		}
		libfiles::document_encode_options options;
		options.quality = settings.JpegQuality;
		options.progressive = settings.JpegProgressive;
		options.optimize = settings.JpegOptimize;
		options.legacy_compatibility = format == libfiles::image_format::jpeg ||
			format == libfiles::image_format::tiff;
		options.maximum_working_bytes = std::uint64_t{1} << 30;
		options.cancelled = [status] { return status->QueryCancel() != FALSE; };
		options.progress = [status](const std::uint32_t current, const std::uint32_t total)
		{
			status->Progress(static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
				static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
		};
		libfiles::document_encode_result encoded;
		try
		{
			encoded = libfiles::encode_document(converted.document, format, options);
		}
		catch (const std::bad_alloc&)
		{
			status->SetError(L"Not enough memory to encode the image.");
			return false;
		}
		catch (const std::exception& error)
		{
			status->SetError(CString(error.what()).GetString());
			return false;
		}
		for (const auto& warning : encoded.warnings)
			status->SetWarning(CString(CA2W(warning.c_str(), CP_UTF8)));
		if (!encoded.success)
		{
			if (encoded.cancelled || status->QueryCancel())
				status->SetError(L"Image encoding was cancelled.");
			else if (encoded.resource_limit)
				status->SetError(L"Not enough memory to encode the image.");
			else if (encoded.unsupported)
				status->SetError(L"The image storage is not representable in this format.");
			else
				status->SetError(CString(CA2W(encoded.error.c_str(), CP_UTF8)));
			return false;
		}
		if (status->QueryCancel())
		{
			status->SetError(L"Image encoding was cancelled.");
			return false;
		}
		return legacy_detail::write_stream_data(output, encoded.data);
	}
}
