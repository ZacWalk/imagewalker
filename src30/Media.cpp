// ImageWalker by Zac Walker
// Reads local audio/video information and opaque BGRA poster frames with Media Foundation.

#include "PlatformWin32.h"
#include "Media.h"
#include "MediaRuntime.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

namespace iw::media
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		constexpr DWORD mediaSourceIndex = static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);
		constexpr DWORD allStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
		constexpr DWORD firstVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

		class Runtime
		{
		public:
			Runtime() : api_(native::acquire()), apartment_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
				result_(!api_ ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) :
					SUCCEEDED(apartment_) || apartment_ == RPC_E_CHANGED_MODE
						? api_->startup(MF_VERSION, MFSTARTUP_FULL) : apartment_) {}
			~Runtime()
			{
				if (SUCCEEDED(result_)) api_->shutdown();
				if (SUCCEEDED(apartment_)) CoUninitialize();
			}
			HRESULT result() const { return result_; }
			const std::shared_ptr<const native::Api>& api() const { return api_; }
		private:
			std::shared_ptr<const native::Api> api_;
			HRESULT apartment_;
			HRESULT result_;
		};

		std::wstring failure(const HRESULT result)
		{
			return std::format(L"Windows could not read this media (0x{:08X}). The file may be damaged "
				L"or require a codec that is not installed.", static_cast<unsigned long>(result));
		}

		HRESULT open_reader(const native::Api& api, const std::filesystem::path& path, const bool videoProcessing,
		                    ComPtr<IMFSourceReader>& reader)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(files::native_path(path), error))
				return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
			ComPtr<IMFAttributes> attributes;
			HRESULT result = api.createAttributes(&attributes, 1);
			if (SUCCEEDED(result))
				result = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, videoProcessing);
			ComPtr<IMFByteStream> stream;
			if (SUCCEEDED(result))
				result = api.createFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE,
					files::native_path(path).c_str(), &stream);
			if (SUCCEEDED(result))
				result = api.createSourceReader(stream.Get(), attributes.Get(), &reader);
			return result;
		}

		std::wstring codec_name(const GUID& subtype)
		{
			if (subtype == MFAudioFormat_PCM) return L"PCM";
			if (subtype == MFAudioFormat_Float) return L"IEEE float";
			if (subtype == MFAudioFormat_MP3) return L"MP3";
			if (subtype == MFAudioFormat_AAC) return L"AAC";
			if (subtype == MFAudioFormat_WMAudioV8) return L"Windows Media Audio";
			if (subtype == MFAudioFormat_WMAudioV9) return L"Windows Media Audio Pro";
			if (subtype == MFAudioFormat_WMAudio_Lossless) return L"Windows Media Audio Lossless";
			// Video subtypes use a FOURCC in the standard media subtype GUID.
			GUID base = MFVideoFormat_H264;
			base.Data1 = subtype.Data1;
			if (base == subtype)
			{
				std::wstring fourcc;
				for (unsigned int index = 0; index < 4; ++index)
				{
					const auto ch = static_cast<wchar_t>((subtype.Data1 >> (8 * index)) & 0xff);
					if (ch < 32 || ch > 126) { fourcc.clear(); break; }
					fourcc.push_back(ch);
				}
				if (!fourcc.empty()) return fourcc;
			}
			wchar_t text[40]{};
			StringFromGUID2(subtype, text, static_cast<int>(std::size(text)));
			return text;
		}

		bool valid_size(const UINT32 width, const UINT32 height)
		{
			return width > 0 && height > 0 && width <= 16384 && height <= 16384 &&
				static_cast<std::uint64_t>(width) * height <= 64 * 1024 * 1024;
		}
	}

	bool is_media(const files::ItemKind kind)
	{
		return kind == files::ItemKind::video || kind == files::ItemKind::audio;
	}

	std::wstring duration_text(const std::int64_t ticks)
	{
		const auto seconds = (std::max)(std::int64_t{0}, ticks) / ticksPerSecond;
		return seconds >= 3600 ? std::format(L"{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60)
			: std::format(L"{}:{:02}", seconds / 60, seconds % 60);
	}

	double frame_rate(const std::uint32_t numerator, const std::uint32_t denominator)
	{
		if (!numerator || !denominator) return 0;
		const double result = static_cast<double>(numerator) / denominator;
		return result >= 0.001 && result <= 1000 ? result : 0;
	}

	Info probe(const std::filesystem::path& path, const std::stop_token& stop)
	{
		Info info;
		if (stop.stop_requested()) return info;
		Runtime runtime;
		if (!runtime.api()) { info.error = native::unavailable_message(); return info; }
		ComPtr<IMFSourceReader> reader;
		HRESULT result = runtime.result();
		if (SUCCEEDED(result)) result = open_reader(*runtime.api(), path, false, reader);
		if (FAILED(result)) { info.error = failure(result); return info; }
		PROPVARIANT duration{};
		if (SUCCEEDED(reader->GetPresentationAttribute(mediaSourceIndex, MF_PD_DURATION, &duration)) &&
			duration.vt == VT_UI8 &&
			duration.uhVal.QuadPart <= static_cast<ULONGLONG>((std::numeric_limits<std::int64_t>::max)()))
		{
			info.hasDuration = true;
			info.duration = static_cast<std::int64_t>(duration.uhVal.QuadPart);
		}
		PropVariantClear(&duration);
		for (DWORD stream = 0; stream < 64 && !stop.stop_requested(); ++stream)
		{
			ComPtr<IMFMediaType> type;
			result = reader->GetNativeMediaType(stream, 0, &type);
			if (result == MF_E_INVALIDSTREAMNUMBER) break;
			if (FAILED(result)) continue;
			GUID major{}, subtype{};
			if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major))) continue;
			const bool video = major == MFMediaType_Video;
			const bool audio = major == MFMediaType_Audio;
			if ((!video && !audio) || (video && info.hasVideo) || (audio && info.hasAudio)) continue;
			if (video)
			{
				info.hasVideo = true;
				UINT32 width{}, height{}, numerator{}, denominator{};
				if (SUCCEEDED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) &&
					valid_size(width, height))
				{
					info.width = static_cast<int>(width);
					info.height = static_cast<int>(height);
				}
				if (SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)))
					info.frameRate = frame_rate(numerator, denominator);
			}
			if (audio) info.hasAudio = true;
			if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)))
			{
				if (!info.codec.empty()) info.codec += L"; ";
				info.codec += (video ? L"Video: " : L"Audio: ") + codec_name(subtype);
			}
		}
		if (stop.stop_requested()) return {};
		if (!info.hasVideo && !info.hasAudio) info.error = failure(MF_E_INVALIDMEDIATYPE);
		return info;
	}

	files::DecodedImage poster(const std::filesystem::path& path, const int maximumWidth,
	                          const int maximumHeight, const std::stop_token& stop)
	{
		files::DecodedImage image;
		if (maximumWidth <= 0 || maximumHeight <= 0 || stop.stop_requested()) return image;
		Runtime runtime;
		ComPtr<IMFSourceReader> reader;
		if (FAILED(runtime.result()) || FAILED(open_reader(*runtime.api(), path, true, reader))) return image;
		if (FAILED(reader->SetStreamSelection(allStreams, FALSE)) ||
			FAILED(reader->SetStreamSelection(firstVideoStream, TRUE))) return image;
		ComPtr<IMFMediaType> output;
		if (FAILED(runtime.api()->createMediaType(&output)) ||
			FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
			FAILED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
			FAILED(reader->SetCurrentMediaType(firstVideoStream, nullptr, output.Get())))
			return image;

		for (int attempt = 0; attempt < 128 && !stop.stop_requested(); ++attempt)
		{
			DWORD flags{};
			LONGLONG timestamp{};
			ComPtr<IMFSample> sample;
			if (FAILED(reader->ReadSample(firstVideoStream, 0, nullptr,
				&flags, &timestamp, &sample)) || (flags & MF_SOURCE_READERF_ERROR)) return {};
			if (!sample)
			{
				if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return {};
				continue;
			}
			ComPtr<IMFMediaType> actual;
			UINT32 width{}, height{}, strideBits{};
			if (FAILED(reader->GetCurrentMediaType(firstVideoStream, &actual)) ||
				FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
				!valid_size(width, height)) return {};
			LONG stride{};
			if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideBits)))
				stride = static_cast<LONG>(strideBits);
			else if (FAILED(runtime.api()->getStride(MFVideoFormat_RGB32.Data1, width, &stride)))
				return {};
			ComPtr<IMFMediaBuffer> buffer;
			if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return {};
			ComPtr<IMF2DBuffer> buffer2d;
			buffer.As(&buffer2d);
			BYTE* pixels{};
			DWORD length{};
			const bool twoDimensional = buffer2d && SUCCEEDED(buffer2d->Lock2D(&pixels, &stride));
			if (!twoDimensional && FAILED(buffer->Lock(&pixels, nullptr, &length))) return {};
			struct Unlock
			{
				IMFMediaBuffer* buffer;
				IMF2DBuffer* buffer2d;
				~Unlock() { if (buffer2d) buffer2d->Unlock2D(); else buffer->Unlock(); }
			} unlock{buffer.Get(), twoDimensional ? buffer2d.Get() : nullptr};
			const auto pitch = std::abs(static_cast<std::int64_t>(stride));
			if (!pixels || pitch < static_cast<std::int64_t>(width) * 4 ||
				(!twoDimensional && static_cast<std::uint64_t>(pitch) * height > length)) return {};
			if (!twoDimensional && stride < 0) pixels += pitch * (height - 1);
			const double scale = (std::min)({1.0, static_cast<double>(maximumWidth) / width,
				static_cast<double>(maximumHeight) / height});
			image.width = (std::max)(1, static_cast<int>(width * scale));
			image.height = (std::max)(1, static_cast<int>(height * scale));
			image.originalWidth = static_cast<int>(width);
			image.originalHeight = static_cast<int>(height);
			image.pixels.resize(static_cast<size_t>(image.width) * image.height);
			for (int y = 0; y < image.height; ++y)
			{
				if (stop.stop_requested()) return {};
				const auto sourceY = static_cast<std::int64_t>(y) * height / image.height;
				const BYTE* row = pixels + sourceY * stride;
				for (int x = 0; x < image.width; ++x)
				{
					const auto sourceX = static_cast<std::int64_t>(x) * width / image.width;
					std::uint32_t pixel{};
					std::memcpy(&pixel, row + sourceX * 4, sizeof(pixel));
					image.pixels[static_cast<size_t>(y) * image.width + x] = pixel | 0xff000000;
				}
			}
			return image;
		}
		return {};
	}
}
