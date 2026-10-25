// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// Purpose: Native image storage and presentation adapters, without a format parser.

#include "LegacyPch.h"
#include "LibfilesDocument.h"
#include "MetadataExif.h"
#include <tiffio.h>

#include <array>

namespace iw::imaging::detail
{
	namespace
	{
		static LPCTSTR GetCompressionString(int nCompression)
		{
			LPCTSTR szCompression = g_szEmptyString;

			switch (nCompression)
			{
			case COMPRESSION_CCITTRLE:
				szCompression = _T("CCITTRLE");
				break;
			case COMPRESSION_CCITTFAX3:
				szCompression = _T("CCITTFAX3");
				break;
			case COMPRESSION_CCITTFAX4:
				szCompression = _T("CCITTFAX4");
				break;
			case COMPRESSION_LZW:
				szCompression = _T("LZW");
				break;
			case COMPRESSION_OJPEG:
				szCompression = _T("OJPEG");
				break;
			case COMPRESSION_JPEG:
				szCompression = _T("JPEG");
				break;
			case COMPRESSION_NEXT:
				szCompression = _T("NEXT");
				break;
			case COMPRESSION_CCITTRLEW:
				szCompression = _T("CCITTRLEW");
				break;
			case COMPRESSION_PACKBITS:
				szCompression = _T("PACKBITS");
				break;
			case COMPRESSION_THUNDERSCAN:
				szCompression = _T("THUNDERSCAN");
				break;
			case COMPRESSION_IT8CTPAD:
				szCompression = _T("IT8CTPAD");
				break;
			case COMPRESSION_IT8LW:
				szCompression = _T("IT8LW");
				break;
			case COMPRESSION_IT8MP:
				szCompression = _T("IT8MP");
				break;
			case COMPRESSION_IT8BL:
				szCompression = _T("IT8BL");
				break;
			case COMPRESSION_PIXARFILM:
				szCompression = _T("PIXARFILM");
				break;
			case COMPRESSION_PIXARLOG:
				szCompression = _T("PIXARLOG");
				break;
			case COMPRESSION_DEFLATE:
				szCompression = _T("DEFLATE");
				break;
			case COMPRESSION_ADOBE_DEFLATE:
				szCompression = _T("ADOBE_DEFLATE");
				break;
			case COMPRESSION_DCS:
				szCompression = _T("DCS");
				break;
			case COMPRESSION_JBIG:
				szCompression = _T("JBIG");
				break;
			case COMPRESSION_SGILOG:
				szCompression = _T("SGILOG");
				break;
			case COMPRESSION_SGILOG24:
				szCompression = _T("SGILOG24");
				break;
			}

			return szCompression;
		}

		static LPCTSTR GetPhotometricString(int nPhotometric)
		{
			LPCTSTR szPhotometric = g_szEmptyString;

			switch (nPhotometric)
			{
			case PHOTOMETRIC_MINISWHITE:
				szPhotometric = _T("MINISWHITE");
				break;
			case PHOTOMETRIC_MINISBLACK:
				szPhotometric = _T("MINISBLACK");
				break;
			case PHOTOMETRIC_RGB:
				szPhotometric = _T("RGB");
				break;
			case PHOTOMETRIC_PALETTE:
				szPhotometric = _T("PALETTE");
				break;
			case PHOTOMETRIC_MASK:
				szPhotometric = _T("MASK");
				break;
			case PHOTOMETRIC_SEPARATED:
				szPhotometric = _T("SEPARATED");
				break;
			case PHOTOMETRIC_YCBCR:
				szPhotometric = _T("YCBCR");
				break;
			case PHOTOMETRIC_CIELAB:
				szPhotometric = _T("CIELAB");
				break;
			case PHOTOMETRIC_LOGL:
				szPhotometric = _T("LOGL");
				break;
			case PHOTOMETRIC_LOGLUV:
				szPhotometric = _T("LOGLUV");
				break;
			}

			return szPhotometric;
		}

		// A damaged later directory ends the document after its complete pages.

		IW::PixelFormat pixel_format(const libfiles::document_storage format)
		{
			constexpr IW::PixelFormat::Format formats[] = {
				IW::PixelFormat::PF1, IW::PixelFormat::PF2, IW::PixelFormat::PF4,
				IW::PixelFormat::PF8, IW::PixelFormat::PF8Alpha, IW::PixelFormat::PF8GrayScale,
				IW::PixelFormat::PF555, IW::PixelFormat::PF565, IW::PixelFormat::PF24,
				IW::PixelFormat::PF32, IW::PixelFormat::PF32Alpha};
			const auto index = static_cast<std::size_t>(format);
			if (index >= std::size(formats)) throw std::invalid_argument("Invalid decoded pixel format.");
			return formats[index];
		}

		class Input final : public libfiles::stream
		{
		public:
			explicit Input(IW::IStreamIn& input) : input_(input) {}
			std::uint64_t size() override { return input_.GetFileSize(); }
			std::size_t read(std::uint64_t offset, void* data, std::size_t size) override
			{
				if (offset > LONG_MAX || input_.Seek(IW::IStreamCommon::eBegin,
					static_cast<LONG>(offset)) != offset) return 0;
				DWORD count{};
				input_.Read(data, static_cast<DWORD>((std::min)(size, size_t{MAXDWORD})), &count);
				return count;
			}
		private:
			IW::IStreamIn& input_;
		};

		class Output final : public libfiles::document_sink
		{
		public:
			Output(IW::IImageStream& output, IW::IStatus& status) : output_(output), status_(status) {}
			bool begin_frame(const libfiles::document_frame& frame) override
			{
				if (first_size_.cx == 0) first_size_ = CSize(frame.width, frame.height);
				const auto right = static_cast<std::int64_t>(frame.left) + frame.width;
				const auto bottom = static_cast<std::int64_t>(frame.top) + frame.height;
				if (right > INT_MAX || bottom > INT_MAX) throw std::bad_alloc();
				output_.CreatePage(CRect(frame.left, frame.top, static_cast<int>(right),
					static_cast<int>(bottom)), pixel_format(frame.storage), frame.sequential);
				if (!frame.palette.empty())
				{
					std::array<COLORREF, 256> palette{};
					if (frame.palette.size() > palette.size())
						throw std::invalid_argument("Invalid decoded palette.");
					std::copy(frame.palette.begin(), frame.palette.end(), palette.begin());
					output_.SetPalette(palette.data());
				}
				return true;
			}
			void row(std::uint32_t y, std::span<const std::uint8_t> bytes) override
			{
				output_.SetBitmap(static_cast<int>(y), bytes.data());
			}
			void end_frame(const libfiles::document_frame& frame) override
			{
				if (frame.transparent) output_.SetTransparent(*frame.transparent);
				if (frame.background) output_.SetBackGround(*frame.background);
				if (frame.delay_ms) output_.SetTimeDelay(*frame.delay_ms);
				if (frame.disposal) output_.SetDisposalMethod(*frame.disposal);
				output_.Flush();
			}
			bool cancelled() override { return status_.QueryCancel(); }
			void progress(std::uint32_t current, std::uint32_t total) override
			{
				status_.Progress(static_cast<int>(current), static_cast<int>(total));
			}
			void metadata(libfiles::document_metadata kind, std::span<const std::uint8_t> bytes) override
			{
				if (bytes.size() > INT_MAX) throw std::bad_alloc();
				if (kind == libfiles::document_metadata::iptc)
					iptc_.CopyData(bytes.data(), static_cast<int>(bytes.size()));
				else if (kind == libfiles::document_metadata::xmp)
					xmp_.CopyData(bytes.data(), static_cast<int>(bytes.size()));
				else
				{
					DWORD type = IW::MetaDataTypes::PROFILE_COMMENT;
					switch (kind)
					{
					case libfiles::document_metadata::exif:
						type = IW::MetaDataTypes::PROFILE_EXIF;
						Exif::Parse(&camera_, bytes.data(), static_cast<unsigned>(bytes.size()));
						break;
					case libfiles::document_metadata::icc: type = IW::MetaDataTypes::PROFILE_ICC; break;
					case libfiles::document_metadata::source_icc: type = IW::MetaDataTypes::PROFILE_SOURCE_ICC; break;
					default: break;
					}
					IW::MetaData blob(type, bytes.data(), static_cast<DWORD>(bytes.size()));
					output_.AddBlob(blob);
				}
			}
			void metadata_complete() override
			{
				output_.AddMetaDataBlob(iptc_, xmp_);
				iptc_ = IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC);
				xmp_ = IW::MetaData(IW::MetaDataTypes::PROFILE_XMP);
			}
			IW::CameraSettings camera_settings() const { return camera_; }
			CSize first_size() const { return first_size_; }
		private:
			IW::IImageStream& output_;
			IW::IStatus& status_;
			IW::MetaData iptc_{IW::MetaDataTypes::PROFILE_IPTC};
			IW::MetaData xmp_{IW::MetaDataTypes::PROFILE_XMP};
			IW::CameraSettings camera_;
			CSize first_size_{0, 0};
		};
	}

	bool read_libfiles_document(const libfiles::image_format format, const CString& key,
		IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status)
	{
		Input source(*input);
		const auto memory = input->GetMemory();
		libfiles::reader reader = memory.empty() ? libfiles::reader(source) : libfiles::reader(memory);
		Output destination(*output, *status);
		libfiles::document_options options;
		const auto thumbnail = output->GetThumbnailSize();
		options.thumbnail_width = static_cast<std::uint32_t>((std::max)(0L, thumbnail.cx));
		options.thumbnail_height = static_cast<std::uint32_t>((std::max)(0L, thumbnail.cy));
		options.pixels = output->WantBitmap();
		const auto result = libfiles::decode_document(reader, format, destination, options);
		for (const auto& warning : result.warnings) status->SetWarning(CString(CA2W(warning.c_str(), CP_UTF8)));
		if (!result.success)
		{
			const CString message = result.cancelled ? CString(IW::Text::CANCELED) :
				result.refused ? CString(L"The decoded image exceeds the destination limit.") :
				CString(CA2W(result.error.c_str(), CP_UTF8));
			if (result.resource_limit) status->SetResourceError(message);
			else status->SetError(message);
			return false;
		}
		if (format == libfiles::image_format::wpg) return true;
		auto settings = destination.camera_settings();
		settings.OriginalImageSize = CSize(result.info.width, result.info.height);
		settings.OriginalBpp = pixel_format(result.info.storage);
		if (result.info.has_resolution)
		{
			settings.XPelsPerMeter = result.info.pixels_per_meter_x;
			settings.YPelsPerMeter = result.info.pixels_per_meter_y;
		}
		output->SetCameraSettings(settings);
		output->SetLoaderName(key);
		CString statistics;
		if (format == libfiles::image_format::pcx)
			statistics.Format(L"%dx%dx%d PCX", result.info.width, result.info.height, result.info.bits_per_pixel);
		else if (format == libfiles::image_format::bmp)
			statistics.Format(IW::Text::BMP_FMT, result.info.width, result.info.height, result.info.bits_per_pixel);
		else if (format == libfiles::image_format::png)
		{
			if (result.info.interlace_passes > 1)
				statistics.Format(IW::Text::PNG_INTERLACED_FMT, result.info.width, result.info.height,
					result.info.bits_per_pixel, result.info.interlace_passes);
			else
				statistics.Format(IW::Text::PNG_FMT, result.info.width, result.info.height, result.info.bits_per_pixel);
			if (output->WantRawImage()) output->AddImageData(input, IW::MetaDataTypes::PNG_IMAGE);
		}
		else if (format == libfiles::image_format::jpeg)
		{
			if (result.info.version)
				statistics.Format(L"%dx%dx%d Jpeg JFIF %d.%d", result.info.width, result.info.height,
					result.info.bits_per_pixel, result.info.version >> 8, result.info.version & 255);
			else
				statistics.Format(L"%dx%dx%d Jpeg", result.info.width, result.info.height, result.info.bits_per_pixel);
			if (output->WantRawImage()) output->AddImageData(input, IW::MetaDataTypes::JPEG_IMAGE);
		}
		else if (format == libfiles::image_format::gif)
		{
			statistics.Format(L"%dx%dx%d GIF", result.info.width, result.info.height, result.info.bits_per_pixel);
			if (result.info.interlace_passes > 1) statistics += CString(L" ") + CString(IW::Text::INTERLACED);
			if (result.info.frames > 1) statistics += CString(L" ") + CString(IW::Text::ANIMATED);
			output->SetImageFlags(IW::ImageFlags::Animate);
			output->AddImageData(input, IW::MetaDataTypes::GIF_IMAGE);
		}
		else if (format == libfiles::image_format::tiff)
		{
			const auto first = destination.first_size();
			statistics.Format(IW::Text::TIFF_FMT, first.cx, first.cy, result.info.bits_per_pixel,
				GetCompressionString(result.info.encoding), GetPhotometricString(result.info.color_model));
		}
		else if (format == libfiles::image_format::psd)
		{
			constexpr const wchar_t* modes[] = {L"Bitmap", L"Grayscale", L"Indexed", L"RGB", L"CMYK",
				L"??", L"??", L"Multichannel", L"Duotone", L"Lab"};
			const auto* mode = result.info.color_model < std::size(modes) ? modes[result.info.color_model] : L"??";
			statistics.Format(IW::Text::PSD_FMT, result.info.width, result.info.height, result.info.bits_per_pixel,
				result.info.encoding == 1 ? L" RLE" : L" ", mode);
		}
		output->SetStatistics(statistics);
		return true;
	}
}
