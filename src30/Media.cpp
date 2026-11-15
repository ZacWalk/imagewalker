// ImageWalker by Zac Walker
// Reads local audio/video information, opaque BGRA poster frames, and video and PCM for playback with Media
// Foundation's source reader.

#include "PlatformWin32.h"
#include "Media.h"
#include "MediaRuntime.h"
#include "Platform.h"

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
		constexpr DWORD firstAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

		class Runtime
		{
		public:
			Runtime() :
				api_(native::acquire()), apartment_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
				result_(!api_ ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
			            : SUCCEEDED(apartment_) || apartment_ == RPC_E_CHANGED_MODE
			                ? api_->startup(MF_VERSION, MFSTARTUP_FULL)
			                : apartment_)
			{
			}
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
			                   L"or require a codec that is not installed.",
			                   static_cast<unsigned long>(result));
		}

		// What the reader converts video to: nothing, RGB32, or RGB32 at another frame size.
		enum class VideoConversion
		{
			none,
			rgb,
			scaledRgb
		};

		HRESULT open_reader(const native::Api& api, const std::filesystem::path& path, const VideoConversion conversion,
		                    ComPtr<IMFSourceReader>& reader)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(files::native_path(path), error))
				return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
			ComPtr<IMFAttributes> attributes;
			HRESULT result = api.createAttributes(&attributes, 1);
			if (SUCCEEDED(result))
				result = conversion == VideoConversion::scaledRgb
				             ? attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)
				             : attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
				                                     conversion == VideoConversion::rgb);
			ComPtr<IMFByteStream> stream;
			if (SUCCEEDED(result))
				result = api.createFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE,
				                        files::native_path(path).c_str(), &stream);
			if (SUCCEEDED(result)) result = api.createSourceReader(stream.Get(), attributes.Get(), &reader);
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
					if (ch < 32 || ch > 126)
					{
						fourcc.clear();
						break;
					}
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

		// A sample's RGB32 pixels, locked for reading: row(y) is the picture's row y, top down.
		class LockedPicture
		{
		public:
			LockedPicture(IMFSample& sample, const UINT32 width, const UINT32 height, LONG stride)
			{
				if (FAILED(sample.ConvertToContiguousBuffer(&buffer_))) return;
				buffer_.As(&buffer2d_);
				BYTE* pixels{};
				DWORD length{};
				locked2d_ = buffer2d_ && SUCCEEDED(buffer2d_->Lock2D(&pixels, &stride));
				if (!locked2d_)
				{
					if (FAILED(buffer_->Lock(&pixels, nullptr, &length))) return;
					locked_ = true;
				}
				const auto pitch = std::abs(static_cast<std::int64_t>(stride));
				if (!pixels || pitch < static_cast<std::int64_t>(width) * 4 ||
				    (!locked2d_ && static_cast<std::uint64_t>(pitch) * height > length))
					return;
				// A contiguous buffer with a negative stride starts with the bottom row.
				if (!locked2d_ && stride < 0) pixels += pitch * (height - 1);
				first_ = pixels;
				stride_ = stride;
			}

			~LockedPicture()
			{
				if (locked2d_) buffer2d_->Unlock2D();
				else if (locked_) buffer_->Unlock();
			}

			LockedPicture(const LockedPicture&) = delete;
			LockedPicture& operator=(const LockedPicture&) = delete;

			explicit operator bool() const { return first_ != nullptr; }
			const BYTE* row(const std::int64_t y) const { return first_ + y * stride_; }

		private:
			ComPtr<IMFMediaBuffer> buffer_;
			ComPtr<IMF2DBuffer> buffer2d_;
			const BYTE* first_{};
			std::int64_t stride_{};
			bool locked_{};
			bool locked2d_{};
		};
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
		if (!runtime.api())
		{
			info.error = native::unavailable_message();
			return info;
		}
		ComPtr<IMFSourceReader> reader;
		HRESULT result = runtime.result();
		if (SUCCEEDED(result)) result = open_reader(*runtime.api(), path, VideoConversion::none, reader);
		if (FAILED(result))
		{
			info.error = failure(result);
			return info;
		}
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

	files::DecodedImage poster(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight,
	                           const std::stop_token& stop)
	{
		files::DecodedImage image;
		if (maximumWidth <= 0 || maximumHeight <= 0 || stop.stop_requested()) return image;
		Runtime runtime;
		ComPtr<IMFSourceReader> reader;
		if (FAILED(runtime.result()) || FAILED(open_reader(*runtime.api(), path, VideoConversion::rgb, reader)))
			return image;
		if (FAILED(reader->SetStreamSelection(allStreams, FALSE)) ||
		    FAILED(reader->SetStreamSelection(firstVideoStream, TRUE)))
			return image;
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
			if (FAILED(reader->ReadSample(firstVideoStream, 0, nullptr, &flags, &timestamp, &sample)) ||
			    (flags & MF_SOURCE_READERF_ERROR))
				return {};
			if (!sample)
			{
				if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return {};
				continue;
			}
			ComPtr<IMFMediaType> actual;
			UINT32 width{}, height{}, strideBits{};
			if (FAILED(reader->GetCurrentMediaType(firstVideoStream, &actual)) ||
			    FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
			    !valid_size(width, height))
				return {};
			LONG stride{};
			if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideBits))) stride = static_cast<LONG>(strideBits);
			else if (FAILED(runtime.api()->getStride(MFVideoFormat_RGB32.Data1, width, &stride))) return {};
			const LockedPicture pixels(*sample.Get(), width, height, stride);
			if (!pixels) return {};
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
				const BYTE* row = pixels.row(sourceY);
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

	struct Decoder::Impl
	{
		// Declared first, so the reader is released before Media Foundation shuts down.
		Runtime runtime;
		ComPtr<IMFSourceReader> reader;
		std::wstring error;
		std::optional<std::int64_t> duration;
		UINT32 width{};
		UINT32 height{};
		LONG stride{};
		bool video{};
		bool audioStream{};
		bool audio{};
		bool seekable{};

		bool fail(const HRESULT result)
		{
			error = native::playback_failure(result);
			return false;
		}

		HRESULT rgb32_type(ComPtr<IMFMediaType>& type) const
		{
			HRESULT result = runtime.api()->createMediaType(&type);
			if (SUCCEEDED(result)) result = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			if (SUCCEEDED(result)) result = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			return result;
		}

		// RGB32, at a width that makes the pixels square when the file's are not.
		HRESULT configure_video()
		{
			ComPtr<IMFMediaType> native;
			HRESULT result = reader->GetNativeMediaType(firstVideoStream, 0, &native);
			if (FAILED(result)) return result;
			UINT32 nativeWidth{}, nativeHeight{}, numerator{}, denominator{};
			if (SUCCEEDED(MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nativeWidth, &nativeHeight)) &&
			    valid_size(nativeWidth, nativeHeight) &&
			    SUCCEEDED(MFGetAttributeRatio(native.Get(), MF_MT_PIXEL_ASPECT_RATIO, &numerator, &denominator)) &&
			    numerator && denominator && numerator != denominator)
			{
				const auto square = static_cast<UINT32>(
										std::llround(static_cast<double>(nativeWidth) * numerator / denominator)) &
				                    ~1u;
				ComPtr<IMFMediaType> scaled;
				if (valid_size(square, nativeHeight) && SUCCEEDED(rgb32_type(scaled)) &&
				    SUCCEEDED(MFSetAttributeSize(scaled.Get(), MF_MT_FRAME_SIZE, square, nativeHeight)) &&
				    SUCCEEDED(MFSetAttributeRatio(scaled.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) &&
				    SUCCEEDED(reader->SetCurrentMediaType(firstVideoStream, nullptr, scaled.Get())) &&
				    read_video_format())
					return S_OK;
			}
			ComPtr<IMFMediaType> output;
			result = rgb32_type(output);
			if (SUCCEEDED(result)) result = reader->SetCurrentMediaType(firstVideoStream, nullptr, output.Get());
			if (SUCCEEDED(result) && !read_video_format()) result = MF_E_INVALIDMEDIATYPE;
			return result;
		}

		bool read_video_format()
		{
			ComPtr<IMFMediaType> current;
			UINT32 currentWidth{}, currentHeight{}, strideBits{};
			if (FAILED(reader->GetCurrentMediaType(firstVideoStream, &current)) ||
			    FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &currentWidth, &currentHeight)) ||
			    !valid_size(currentWidth, currentHeight))
				return false;
			LONG currentStride{};
			if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideBits)))
				currentStride = static_cast<LONG>(strideBits);
			else if (FAILED(runtime.api()->getStride(MFVideoFormat_RGB32.Data1, currentWidth, &currentStride)))
				return false;
			width = currentWidth;
			height = currentHeight;
			stride = currentStride;
			return true;
		}

		// The next sample of a stream: none at its end, and none with an error once it fails.
		ReadResult read(const DWORD stream, ComPtr<IMFSample>& sample, LONGLONG& time)
		{
			// Stream ticks and gaps arrive without a sample; a stream that sends nothing else is broken.
			for (int attempt = 0; attempt < 1024; ++attempt)
			{
				DWORD flags{};
				sample.Reset();
				const HRESULT result = reader->ReadSample(stream, 0, nullptr, &flags, &time, &sample);
				if (FAILED(result) || (flags & MF_SOURCE_READERF_ERROR))
				{
					fail(FAILED(result) ? result : E_FAIL);
					return ReadResult::failed;
				}
				if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && stream == firstVideoStream &&
				    !read_video_format())
				{
					fail(MF_E_INVALIDMEDIATYPE);
					return ReadResult::failed;
				}
				if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return ReadResult::end;
				if (sample) return ReadResult::sample;
			}
			fail(MF_E_INVALID_STREAM_DATA);
			return ReadResult::failed;
		}
	};

	Decoder::Decoder() : impl_(std::make_unique<Impl>())
	{
	}

	Decoder::~Decoder() = default;

	bool Decoder::open(const std::filesystem::path& path)
	{
		auto& d = *impl_;
		d.reader.Reset();
		d.error.clear();
		d.video = d.audioStream = d.audio = d.seekable = false;
		d.duration.reset();
		if (!d.runtime.api())
		{
			d.error = native::unavailable_message();
			return false;
		}
		HRESULT result = d.runtime.result();
		if (SUCCEEDED(result)) result = open_reader(*d.runtime.api(), path, VideoConversion::scaledRgb, d.reader);
		if (FAILED(result)) return d.fail(result);
		ComPtr<IMFMediaType> native;
		d.video = SUCCEEDED(d.reader->GetNativeMediaType(firstVideoStream, 0, &native));
		native.Reset();
		d.audioStream = SUCCEEDED(d.reader->GetNativeMediaType(firstAudioStream, 0, &native));
		result = d.reader->SetStreamSelection(allStreams, FALSE);
		if (FAILED(result)) return d.fail(result);
		if (d.video)
		{
			result = d.configure_video();
			if (SUCCEEDED(result)) result = d.reader->SetStreamSelection(firstVideoStream, TRUE);
			if (FAILED(result))
			{
				if (!d.audioStream) return d.fail(result);
				platform::write_diagnostic(std::format(L"The video in {} cannot be decoded (0x{:08X}); only its "
				                                       L"sound plays.\n",
				                                       path.filename().wstring(), static_cast<unsigned long>(result)));
				d.video = false;
			}
		}
		if (!d.video && !d.audioStream) return d.fail(MF_E_INVALIDMEDIATYPE);
		PROPVARIANT value{};
		if (SUCCEEDED(d.reader->GetPresentationAttribute(mediaSourceIndex, MF_PD_DURATION, &value)) &&
		    value.vt == VT_UI8 &&
		    value.uhVal.QuadPart <= static_cast<ULONGLONG>((std::numeric_limits<std::int64_t>::max)()))
			d.duration = static_cast<std::int64_t>(value.uhVal.QuadPart);
		PropVariantClear(&value);
		if (SUCCEEDED(d.reader->GetPresentationAttribute(mediaSourceIndex, MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS,
		                                                 &value)) &&
		    value.vt == VT_UI4)
			d.seekable = (value.ulVal & MFMEDIASOURCE_CAN_SEEK) != 0;
		PropVariantClear(&value);
		return true;
	}

	bool Decoder::decode_audio(const AudioFormat& format)
	{
		auto& d = *impl_;
		if (!d.reader || !d.audioStream || !format.sampleRate || !format.channels) return false;
		const UINT32 bits = format.format == SampleFormat::int16 ? 16 : 32;
		const GUID subtype = format.format == SampleFormat::float32 ? MFAudioFormat_Float : MFAudioFormat_PCM;
		ComPtr<IMFMediaType> type;
		HRESULT result = d.runtime.api()->createMediaType(&type);
		if (SUCCEEDED(result)) result = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		if (SUCCEEDED(result)) result = type->SetGUID(MF_MT_SUBTYPE, subtype);
		if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, format.channels);
		if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, format.sampleRate);
		if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
		if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, format.frame_bytes());
		if (SUCCEEDED(result))
			result = type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, format.sampleRate * format.frame_bytes());
		if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
		if (SUCCEEDED(result) && format.channelMask)
			result = type->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, static_cast<UINT32>(format.channelMask));
		if (SUCCEEDED(result)) result = d.reader->SetCurrentMediaType(firstAudioStream, nullptr, type.Get());
		// A reader that settled on a format near the one asked for would play at the wrong speed.
		ComPtr<IMFMediaType> actual;
		if (SUCCEEDED(result)) result = d.reader->GetCurrentMediaType(firstAudioStream, &actual);
		GUID actualSubtype{};
		if (SUCCEEDED(result) &&
		    (FAILED(actual->GetGUID(MF_MT_SUBTYPE, &actualSubtype)) || actualSubtype != subtype ||
		     MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) != format.sampleRate ||
		     MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0) != format.channels ||
		     MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0) != bits))
			result = MF_E_INVALIDMEDIATYPE;
		if (SUCCEEDED(result)) result = d.reader->SetStreamSelection(firstAudioStream, TRUE);
		d.audio = SUCCEEDED(result);
		if (!d.audio)
			platform::write_diagnostic(std::format(L"Sound cannot be converted to the output's {} Hz, {} channels "
			                                       L"(0x{:08X}).\n",
			                                       format.sampleRate, format.channels,
			                                       static_cast<unsigned long>(result)));
		return d.audio;
	}

	const std::wstring& Decoder::error() const
	{
		return impl_->error;
	}

	bool Decoder::has_video() const
	{
		return impl_->video;
	}

	bool Decoder::has_audio() const
	{
		return impl_->audioStream;
	}

	bool Decoder::can_seek() const
	{
		return impl_->seekable;
	}

	std::optional<std::int64_t> Decoder::duration() const
	{
		return impl_->duration;
	}

	ReadResult Decoder::read_video(VideoFrame& frame)
	{
		auto& d = *impl_;
		if (!d.reader || !d.video) return ReadResult::end;
		ComPtr<IMFSample> sample;
		LONGLONG time{};
		const auto read = d.read(firstVideoStream, sample, time);
		if (read != ReadResult::sample) return read;
		const LockedPicture pixels(*sample.Get(), d.width, d.height, d.stride);
		if (!pixels)
		{
			d.fail(MF_E_INVALIDMEDIATYPE);
			return ReadResult::failed;
		}
		if (!frame.picture) frame.picture = std::make_shared<::core::surface>();
		frame.picture->alloc({static_cast<int>(d.width), static_cast<int>(d.height)}, ::core::texture_format::RGB);
		const auto rowBytes = static_cast<size_t>(d.width) * 4;
		for (UINT32 y = 0; y < d.height; ++y)
			std::memcpy(frame.picture->pixels_line(static_cast<int>(y)), pixels.row(y), rowBytes);
		LONGLONG duration{};
		frame.time = time;
		frame.duration = SUCCEEDED(sample->GetSampleDuration(&duration)) ? duration : 0;
		return ReadResult::sample;
	}

	ReadResult Decoder::read_audio(AudioBlock& block)
	{
		auto& d = *impl_;
		if (!d.reader || !d.audio) return ReadResult::end;
		ComPtr<IMFSample> sample;
		LONGLONG time{};
		const auto read = d.read(firstAudioStream, sample, time);
		if (read != ReadResult::sample) return read;
		ComPtr<IMFMediaBuffer> buffer;
		BYTE* data{};
		DWORD length{};
		HRESULT result = sample->ConvertToContiguousBuffer(&buffer);
		if (SUCCEEDED(result)) result = buffer->Lock(&data, nullptr, &length);
		if (FAILED(result))
		{
			d.fail(result);
			return ReadResult::failed;
		}
		block.samples.assign(data, data + length);
		buffer->Unlock();
		block.time = time;
		return ReadResult::sample;
	}

	bool Decoder::seek(const std::int64_t time)
	{
		auto& d = *impl_;
		if (!d.reader || !d.seekable) return false;
		PROPVARIANT position{};
		position.vt = VT_I8;
		position.hVal.QuadPart = (std::max)(std::int64_t{0}, time);
		return SUCCEEDED(d.reader->SetCurrentPosition(GUID_NULL, position));
	}
}
