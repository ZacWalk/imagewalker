#include "stdafx.h"
#include "LoadAny.h"
#include "dibthumb.h"
#include "iw/logfile.h"

#include <libfiles/decode.h>
#include <libfiles/document.h>
#include <wincodec.h>

namespace
{
	using Format = libfiles::image_format;
	using Storage = libfiles::document_storage;
	constexpr std::uint64_t workingLimit = 1ull << 30;
	constexpr std::uint64_t pixelLimit = (256ull << 20) / 4;

	class FileReader final : public libfiles::stream
	{
	public:
		explicit FileReader(const wchar_t* path)
			: file_(::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
		{
			LARGE_INTEGER size{};
			if (file_ != INVALID_HANDLE_VALUE && ::GetFileType(file_) == FILE_TYPE_DISK &&
				::GetFileSizeEx(file_, &size) && size.QuadPart >= 0)
				length_ = static_cast<std::uint64_t>(size.QuadPart);
		}
		~FileReader() override { if (file_ != INVALID_HANDLE_VALUE) ::CloseHandle(file_); }
		HANDLE Handle() const { return file_; }
		std::uint64_t size() override { return length_; }
		std::size_t read(std::uint64_t offset, void* bytes, std::size_t count) override
		{
			if (offset > length_ || count > length_ - offset) return 0;
			LARGE_INTEGER where{};
			where.QuadPart = static_cast<LONGLONG>(offset);
			DWORD read{};
			if (::SetFilePointerEx(file_, where, nullptr, FILE_BEGIN))
				::ReadFile(file_, bytes, static_cast<DWORD>((std::min)(count, size_t{MAXDWORD})), &read, nullptr);
			return read;
		}

	private:
		HANDLE file_;
		std::uint64_t length_{};
	};

	CSize FitThumbnail(unsigned width, unsigned height, int edge)
	{
		const CSize size(static_cast<LONG>(width), static_cast<LONG>(height));
		return edge > 0 && (size.cx > edge || size.cy > edge) ? FitToBox(size, {edge, edge}) : size;
	}

	class FirstFrame final : public libfiles::document_sink
	{
	public:
		FirstFrame(Format format, CStatus* status, int edge) : format_(format), status_(status), edge_(edge) {}

		bool begin_frame(const libfiles::document_frame& frame) override
		{
			if (complete)
			{
				stoppedAfterFirst = true;
				return false;
			}
			const CSize size = FitThumbnail(frame.width, frame.height, edge_);
			downsample_ = static_cast<unsigned>(size.cx) != frame.width || static_cast<unsigned>(size.cy) != frame.height;

			packed_ = static_cast<unsigned>(libfiles::palette_entries(frame.storage));
			unsigned bits = 32;
			if (!downsample_)
			{
				if (packed_)
					bits = format_ == Format::bmp || format_ == Format::gif || format_ == Format::pcx ||
						frame.storage == Storage::indexed2 ? 8 : libfiles::storage_bits(frame.storage);
				else if (frame.storage == Storage::gray8)
					bits = 8;
				else if ((frame.storage == Storage::bgr24 && format_ != Format::tiff) ||
					(format_ == Format::bmp &&
					 (frame.storage == Storage::bgrx32 || frame.storage == Storage::bgra32)))
					bits = 24;
			}

			if (!dib.Create(size.cx, size.cy, bits, FALSE)) return false;
			source_ = frame;
			if (downsample_ || (!packed_ && bits != libfiles::storage_bits(frame.storage)))
			{
				if (frame.width > pixelLimit) return false;
				row_.resize(frame.width);
			}
			if (packed_ && !downsample_)
			{
				const auto count = (std::min)(static_cast<size_t>(packed_), frame.palette.size());
				if (!count) return false;
				std::memcpy(dib.GetColor(), frame.palette.data(), count * sizeof(RGBQUAD));
				if (frame.transparent && *frame.transparent < count)
					dib.GetColor()[*frame.transparent].rgbReserved = 0;
			}
			return true;
		}

		void row(std::uint32_t y, std::span<const std::uint8_t> bytes) override
		{
			const auto dstY = downsample_ ? ScaleRow(y) : y;
			if (downsample_ && dstY == ScaleRow(y + 1)) return;
			auto* dst = dib.GetBitmap(dstY);
			const auto sourceBits = libfiles::storage_bits(source_.storage);
			if (!downsample_ && packed_ && dib.Bpp() == 8 && sourceBits < 8)
			{
				const unsigned mask = (1u << sourceBits) - 1;
				for (unsigned x = 0; x < source_.width; ++x)
				{
					const unsigned bit = x * sourceBits;
					dst[x] = static_cast<BYTE>((bytes[bit / 8] >> (8 - sourceBits - (bit % 8))) & mask);
				}
				std::memset(dst + source_.width, 0, dib.m_nStorageWidth - source_.width);
				return;
			}
			if (!downsample_ && dib.Bpp() == sourceBits && source_.storage != Storage::bgrx32)
			{
				std::memcpy(dst, bytes.data(), dib.m_nStorageWidth);
				return;
			}
			const libfiles::pixel_rows pixels{source_.width, 1, source_.storage, bytes.size(), false,
			                                  bytes, source_.palette, source_.transparent};
			if (row_.empty())
				libfiles::copy_bgra(pixels, {reinterpret_cast<std::uint32_t*>(dst), dib.Width()}, dib.Width(), false);
			else
			{
				libfiles::copy_bgra(pixels, row_, source_.width, false);
				if (dib.Bpp() == 32)
					for (unsigned x = 0; x < dib.Width(); ++x)
						reinterpret_cast<std::uint32_t*>(dst)[x] = row_[static_cast<size_t>(
							static_cast<std::uint64_t>(x) * source_.width / dib.Width())];
				else
				{
					for (unsigned x = 0; x < dib.Width(); ++x)
					{
						const auto pixel = row_[x];
						dst[x * 3] = static_cast<BYTE>(pixel);
						dst[x * 3 + 1] = static_cast<BYTE>(pixel >> 8);
						dst[x * 3 + 2] = static_cast<BYTE>(pixel >> 16);
					}
					std::memset(dst + dib.Width() * 3, 0, dib.m_nStorageWidth - dib.Width() * 3);
				}
			}
		}

		void end_frame(const libfiles::document_frame&) override { complete = true; }
		bool cancelled() override { return status_ && status_->Exit(); }
		void progress(std::uint32_t current, std::uint32_t total) override
		{
			if (status_) status_->Status(static_cast<int>(current), static_cast<int>(total));
		}

		CDib dib;
		bool complete = false;
		bool stoppedAfterFirst = false;

	private:
		unsigned ScaleRow(unsigned y)
		{
			// Map this source row back to its destination, even for interlaced GIF.
			return static_cast<unsigned>((static_cast<std::uint64_t>(y) * dib.Height() + source_.height - 1) /
			                             source_.height);
		}

		Format format_;
		CStatus* status_;
		int edge_;
		bool downsample_ = false;
		unsigned packed_ = 0;
		libfiles::document_frame source_;
		std::vector<std::uint32_t> row_;
	};

	bool ReadNative(CDib& dib, libfiles::reader& input, Format format, CStatus* status, int edge)
	{
		libfiles::document_options options;
		options.maximum_working_bytes = workingLimit;
		options.thumbnail_width = options.thumbnail_height = edge > 0 ? static_cast<unsigned>(edge) : 0;
		options.legacy_tiff_storage = true;
		FirstFrame output(format, status, edge);
		const auto result = libfiles::decode_document(input, format, output, options);
		if (result.cancelled || output.cancelled()) return false;
		const bool keepFirst = output.stoppedAfterFirst ||
			(result.truncated && (format == Format::gif || format == Format::tiff));
		for (const auto& warning : result.warnings)
			IW::Logging::Warn("Image decode: %hs", warning.c_str());
		if (!output.complete || (!result.success && !keepFirst))
		{
			IW::Logging::Warn("Image decode: %hs", result.error.empty() ? "no complete picture within limits" :
			                  result.error.c_str());
			return false;
		}
		if (!result.error.empty()) IW::Logging::Warn("Image decode: %hs; showing the first frame", result.error.c_str());
		const auto width = result.info.width ? result.info.width : output.dib.Width();
		const auto height = result.info.height ? result.info.height : output.dib.Height();
		if (width > INT_MAX || height > INT_MAX) return false;
		output.dib.m_sizeOriginal = CSize(width, height);
		for (const auto& type : libfiles::file_types())
		{
			if (type.image == format)
			{
				output.dib.m_strInfo = CText(std::string(type.name));
				break;
			}
		}
		dib.Attach(output.dib);
		return true;
	}

	void FlipToBottomUp(CDib& dib)
	{
		const size_t stride = dib.m_nStorageWidth;
		std::vector<BYTE> row(stride);
		for (unsigned y = 0; y < dib.Height() / 2; ++y)
		{
			auto* low = dib.GetBitmap(y);
			auto* high = dib.GetBitmap(dib.Height() - 1 - y);
			std::memcpy(row.data(), low, stride);
			std::memcpy(low, high, stride);
			std::memcpy(high, row.data(), stride);
		}
	}

	bool ReadWebP(CDib& dib, libfiles::reader& input, CStatus* status, int edge)
	{
		struct Sink final : libfiles::image_sink
		{
			explicit Sink(CStatus* status) : status(status) {}
			libfiles::pixel_buffer allocate(const libfiles::frame_info& frame) override
			{
				if (static_cast<std::uint64_t>(frame.width) * frame.height > pixelLimit ||
					!dib.Create(frame.width, frame.height, 32, FALSE))
					return {};
				return {dib.GetBitmap(), static_cast<size_t>(dib.m_nStorageWidth)};
			}
			bool cancelled() override { return status && status->Exit(); }
			CStatus* status;
			CDib dib;
		} output(status);
		libfiles::decode_options options;
		options.target_width = options.target_height = edge > 0 ? static_cast<unsigned>(edge) : 0;
		const auto result = libfiles::decode(input, output, options);
		if (output.cancelled()) return false;
		if (!result.success || !output.dib.IsOpen())
		{
			IW::Logging::Warn("WebP decode: %hs", result.error.empty() ? "image exceeds decode limits" : result.error.c_str());
			return false;
		}
		FlipToBottomUp(output.dib);
		output.dib.m_sizeOriginal = CSize(result.frame.source_width, result.frame.source_height);
		output.dib.m_strInfo = "WebP";
		dib.Attach(output.dib);
		return true;
	}

	HRESULT ReadWindowsCodec(CDib& dib, HANDLE file, CStatus* status, int edge)
	{
		IWICImagingFactory* factoryRaw{};
		HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		                                 IID_PPV_ARGS(&factoryRaw));
		if (FAILED(hr)) return hr;
		const ShellPtr<IWICImagingFactory> factory(factoryRaw);
		LARGE_INTEGER zero{};
		if (!::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(::GetLastError());
		IWICBitmapDecoder* decoderRaw{};
		hr = factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(file), nullptr,
		                                          WICDecodeMetadataCacheOnDemand, &decoderRaw);
		if (FAILED(hr)) return hr;
		const ShellPtr<IWICBitmapDecoder> decoder(decoderRaw);
		IWICBitmapFrameDecode* frameRaw{};
		if (FAILED(hr = decoder->GetFrame(0, &frameRaw))) return hr;
		const ShellPtr<IWICBitmapFrameDecode> frame(frameRaw);
		UINT width{}, height{};
		if (FAILED(hr = frame->GetSize(&width, &height))) return hr;
		if (!width || !height || static_cast<std::uint64_t>(width) * height > pixelLimit)
			return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
		const auto size = FitThumbnail(width, height, edge);
		IWICFormatConverter* converterRaw{};
		if (FAILED(hr = factory->CreateFormatConverter(&converterRaw))) return hr;
		const ShellPtr<IWICFormatConverter> converter(converterRaw);
		if (FAILED(hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
		                                       WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
			return hr;
		IWICBitmapSource* source = converter.get();
		ShellPtr<IWICBitmapScaler> scaler;
		if (static_cast<UINT>(size.cx) != width || static_cast<UINT>(size.cy) != height)
		{
			IWICBitmapScaler* scalerRaw{};
			if (FAILED(hr = factory->CreateBitmapScaler(&scalerRaw))) return hr;
			scaler.reset(scalerRaw);
			if (FAILED(hr = scaler->Initialize(source, size.cx, size.cy, WICBitmapInterpolationModeFant))) return hr;
			source = scaler.get();
		}
		if (status && status->Exit()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
		CDib decoded;
		if (!decoded.Create(size.cx, size.cy, 32, FALSE)) return E_OUTOFMEMORY;
		const auto stride = static_cast<UINT>(decoded.m_nStorageWidth);
		if (FAILED(hr = source->CopyPixels(nullptr, stride, stride * decoded.Height(), decoded.GetBitmap()))) return hr;
		if (status && status->Exit()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
		FlipToBottomUp(decoded);
		decoded.m_sizeOriginal = CSize(width, height);
		decoded.m_strInfo = "Windows image codec";
		dib.Attach(decoded);
		return S_OK;
	}

	Format Identify(libfiles::reader& input, std::string_view extension)
	{
		const auto head = input.view(0, static_cast<size_t>((std::min)(input.size(), std::uint64_t{128})));
		const auto format = libfiles::detect_image_format(head);
		if (format != Format::unknown) return format;
		// PCX has no strong signature. The full header and pixels are still validated by libfiles.
		if (head.size() == 128 && head[0] == 0x0a && head[1] <= 5 && head[2] == 1 && head[3] == 8)
			return Format::pcx;
		return libfiles::info(libfiles::file_type_from_extension(extension)).image;
	}

	BOOL Decode(CDib& dib, libfiles::reader& input, CStatus* status, int edge, LPCSTR path = nullptr,
	            HANDLE file = INVALID_HANDLE_VALUE)
	{
		if (!input.size() || (status && status->Exit())) return FALSE;
		const auto format = Identify(input, path ? ::PathFindExtensionA(path) : "");
		if (libfiles::can_decode_document(format)) return ReadNative(dib, input, format, status, edge);
		if (format == Format::webp) return ReadWebP(dib, input, status, edge);
		if (file == INVALID_HANDLE_VALUE) return FALSE;

		const auto hr = ReadWindowsCodec(dib, file, status, edge);
		if (FAILED(hr) && hr != WINCODEC_ERR_COMPONENTNOTFOUND && hr != WINCODEC_ERR_UNKNOWNIMAGEFORMAT &&
			hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
			IW::Logging::Warn("Windows image decode: HRESULT 0x%08lx", static_cast<unsigned long>(hr));
		return SUCCEEDED(hr);
	}
}

BOOL CLoadAny::Load(CDib* dib, LPBYTE bytes, DWORD size, CStatus* status, BOOL thumbnail)
{
	if (!dib || (!bytes && size))
	{
		IW::Logging::Error("Invalid image decode destination or buffer");
		return FALSE;
	}
	dib->Close();
	try
	{
		libfiles::reader input(std::span<const std::uint8_t>(bytes, size));
		return Decode(*dib, input, status, thumbnail ? IMAGE_X : 0);
	}
	catch (const std::exception& error)
	{
		IW::Logging::Warn("Memory image decode: %hs", error.what());
		return FALSE;
	}
}

BOOL CLoadAny::Load(CDib* dib, LPCSTR path, CStatus* status, BOOL thumbnail, int edge)
{
	if (!dib || !path || !*path || (thumbnail && edge <= 0))
	{
		IW::Logging::Error("Invalid image decode destination, path or thumbnail size");
		return FALSE;
	}
	dib->Close();
	try
	{
		if (status && status->Exit()) return FALSE;
		const auto wide = WideText(path);
		if (wide.empty())
		{
			IW::Logging::LastError("Convert image path to Unicode");
			return FALSE;
		}
		FileReader source(wide.c_str());
		if (source.Handle() == INVALID_HANDLE_VALUE)
		{
			IW::Logging::LastError("Open image for reading");
			return FALSE;
		}
		libfiles::reader input(source);
		return Decode(*dib, input, status, thumbnail ? edge : 0, path, source.Handle());
	}
	catch (const std::exception& error)
	{
		IW::Logging::Warn("File image decode: %hs", error.what());
		return FALSE;
	}
}

BOOL CLoadAny::Load(CDib* dib, int resource, CStatus* status, BOOL thumbnail)
{
	const auto instance = ::GetModuleHandleA(nullptr);
	const auto found = ::FindResourceA(instance, MAKEINTRESOURCEA(resource), "image");
	const auto loaded = found ? ::LoadResource(instance, found) : nullptr;
	const auto size = found ? ::SizeofResource(instance, found) : 0;
	auto* bytes = loaded ? static_cast<BYTE*>(::LockResource(loaded)) : nullptr;
	if (!bytes || !size)
	{
		IW::Logging::Error("Image resource %d could not be loaded", resource);
		return FALSE;
	}
	return Load(dib, bytes, size, status, thumbnail);
}
