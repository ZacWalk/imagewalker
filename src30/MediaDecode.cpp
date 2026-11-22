// ImageWalker by Zac Walker
// Reads compressed media into timed BGRA pictures and interleaved PCM.

#include "MediaDecode.h"
#include "FfmpegInput.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstring>

namespace iw::media
{
	namespace
	{
		using ffmpeg::Input;
		using ffmpeg::tickTime;
		constexpr auto maximumTime = 100LL * 366 * 24 * 3600 * ticksPerSecond;

		bool usable_size(const int width, const int height)
		{
			return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
			       static_cast<std::int64_t>(width) * height <= ffmpeg::maxVideoPixels;
		}

		std::int64_t time_in_ticks(const std::int64_t value, const AVRational timeBase)
		{
			if (value == AV_NOPTS_VALUE || timeBase.num <= 0 || timeBase.den <= 0) return AV_NOPTS_VALUE;
			const auto time = av_rescale_q(value, timeBase, tickTime);
			return time < -maximumTime || time > maximumTime ? AV_NOPTS_VALUE : time;
		}

		const AVStream* primary(const AVFormatContext& input, const AVMediaType type, const bool decodable)
		{
			const AVStream* first = nullptr;
			for (unsigned i = 0; i < input.nb_streams; ++i)
			{
				const auto& stream = *input.streams[i];
				const auto& codec = *stream.codecpar;
				if (codec.codec_type != type || (type == AVMEDIA_TYPE_VIDEO && !ffmpeg::picture(stream))) continue;
				if (decodable && (!avcodec_find_decoder(codec.codec_id) ||
				                  (type == AVMEDIA_TYPE_VIDEO && codec.width > 0 && codec.height > 0 &&
				                   !usable_size(codec.width, codec.height))))
					continue;
				if (stream.disposition & AV_DISPOSITION_DEFAULT) return &stream;
				if (!first) first = &stream;
			}
			return first;
		}

		Info media_info(const AVFormatContext& input)
		{
			Info info;
			const auto time = time_in_ticks(input.duration, ffmpeg::microTime);
			if (time != AV_NOPTS_VALUE && time > 0)
			{
				info.hasDuration = true;
				info.duration = time;
			}
			if (const auto* video = primary(input, AVMEDIA_TYPE_VIDEO, false))
			{
				info.hasVideo = true;
				info.width = video->codecpar->width;
				info.height = video->codecpar->height;
				const auto rate = video->avg_frame_rate;
				if (rate.num > 0 && rate.den > 0) info.frameRate = frame_rate(rate.num, rate.den);
				const std::string_view name = avcodec_get_name(video->codecpar->codec_id);
				info.codec = L"Video: " + std::wstring(name.begin(), name.end());
			}
			if (const auto* audio = primary(input, AVMEDIA_TYPE_AUDIO, false))
			{
				info.hasAudio = true;
				const std::string_view name = avcodec_get_name(audio->codecpar->codec_id);
				if (!info.codec.empty()) info.codec += L"; ";
				info.codec += L"Audio: " + (name.starts_with("pcm_")
				                                ? L"PCM (" + std::wstring(name.begin() + 4, name.end()) + L")"
				                                : std::wstring(name.begin(), name.end()));
			}
			return info;
		}

		::core::simple_transform rotation(const std::uint8_t* bytes, const size_t size)
		{
			std::int32_t matrix[9];
			if (!bytes || size != sizeof(matrix)) return ::core::simple_transform::none;
			std::memcpy(matrix, bytes, sizeof(matrix));
			const auto degrees = av_display_rotation_get(matrix);
			if (!std::isfinite(degrees)) return ::core::simple_transform::none;
			const auto quarters = (std::lround(degrees / 90.0) + 4) % 4;
			constexpr ::core::simple_transform turns[]{::core::simple_transform::none, ::core::simple_transform::rot_90,
			                                           ::core::simple_transform::rot_180,
			                                           ::core::simple_transform::rot_270};
			return turns[quarters];
		}

		struct Track
		{
			std::unique_ptr<Input> input;
			const AVStream* stream{};
			AVCodecContext* codec{};
			AVPacket* packet{};
			AVFrame* frame{};
			bool flushing{};
			std::int64_t nextTime{};

			~Track()
			{
				av_frame_free(&frame);
				av_packet_free(&packet);
				avcodec_free_context(&codec);
			}

			int open(std::unique_ptr<Input> source, const AVStream& selected)
			{
				input = std::move(source);
				stream = &selected;
				const auto* implementation = avcodec_find_decoder(stream->codecpar->codec_id);
				if (!implementation) return AVERROR_DECODER_NOT_FOUND;
				codec = avcodec_alloc_context3(implementation);
				frame = av_frame_alloc();
				packet = av_packet_alloc();
				if (!codec || !frame || !packet) return AVERROR(ENOMEM);
				if (const int result = avcodec_parameters_to_context(codec, stream->codecpar); result < 0)
					return result;
				codec->pkt_timebase = stream->time_base;
				codec->max_pixels = ffmpeg::maxVideoPixels;
				codec->max_samples = 2 * 1024 * 1024;
				const auto pixels = static_cast<std::int64_t>(codec->width) * codec->height;
				codec->thread_count = codec->codec_type == AVMEDIA_TYPE_VIDEO && pixels <= 3840LL * 2160
				                          ? (sizeof(void*) == 8 ? 4 : 2)
				                          : 1;
				const int result = avcodec_open2(codec, implementation, nullptr);
				if (result < 0) return result;
				for (unsigned i = 0; i < input->format->nb_streams; ++i)
					if (input->format->streams[i] != stream) input->format->streams[i]->discard = AVDISCARD_ALL;
				return 0;
			}

			int read()
			{
				av_frame_unref(frame);
				for (int damaged = 0; !input->io.stop.stop_requested();)
				{
					const int result = avcodec_receive_frame(codec, frame);
					if (result >= 0 || result == AVERROR_EOF) return result;
					if (result != AVERROR(EAGAIN) && (result != AVERROR_INVALIDDATA || ++damaged > 32)) return result;
					if (flushing) return result == AVERROR(EAGAIN) ? AVERROR_EOF : result;
					int consumed;
					do
					{
						av_packet_unref(packet);
						consumed = av_read_frame(input->format, packet);
					} while (consumed >= 0 && packet->stream_index != stream->index &&
					         !input->io.stop.stop_requested());
					if (consumed == AVERROR_EOF && input->buffer->error < 0) return input->buffer->error;
					if (consumed == AVERROR_EOF) flushing = true;
					else if (consumed < 0) return consumed;
					const int sent = avcodec_send_packet(codec, flushing ? nullptr : packet);
					av_packet_unref(packet);
					if (sent < 0 && (sent != AVERROR_INVALIDDATA || ++damaged > 32)) return sent;
				}
				return AVERROR_EXIT;
			}

			bool seek(const std::int64_t time, const std::int64_t origin)
			{
				const auto target = av_rescale_q_rnd(time + origin, tickTime, stream->time_base, AV_ROUND_DOWN);
				if (av_seek_frame(input->format, stream->index, target, AVSEEK_FLAG_BACKWARD) < 0) return false;
				avcodec_flush_buffers(codec);
				av_packet_unref(packet);
				av_frame_unref(frame);
				flushing = false;
				nextTime = time;
				return true;
			}

			std::int64_t time(const std::int64_t origin) const
			{
				const auto timestamp = time_in_ticks(frame->best_effort_timestamp, stream->time_base);
				return timestamp != AV_NOPTS_VALUE ? timestamp - origin : nextTime;
			}
		};

		AVSampleFormat audio_format(const SampleFormat format)
		{
			switch (format)
			{
			case SampleFormat::int16: return AV_SAMPLE_FMT_S16;
			case SampleFormat::int32: return AV_SAMPLE_FMT_S32;
			default: return AV_SAMPLE_FMT_FLT;
			}
		}

		int color_matrix(const AVFrame& frame)
		{
			switch (frame.colorspace)
			{
			case AVCOL_SPC_BT709: return SWS_CS_ITU709;
			case AVCOL_SPC_BT2020_NCL:
			case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
			case AVCOL_SPC_SMPTE170M:
			case AVCOL_SPC_BT470BG: return SWS_CS_ITU601;
			case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
			default: return frame.width > 1024 || frame.height > 576 ? SWS_CS_ITU709 : SWS_CS_ITU601;
			}
		}
	}

	struct SoftwareDecoder::Impl
	{
		Info info;
		std::wstring error;
		std::unique_ptr<Track> video, audio;
		std::int64_t origin{};
		SwsContext* scale{};
		SwrContext* resample{};
		AVChannelLayout sourceLayout{};
		AVChannelLayout targetLayout{};
		int sourceRate{};
		int sourceFormat{-1};
		AudioFormat target;
		bool haveAudioTime{};
		::core::surface staging;

		~Impl()
		{
			swr_free(&resample);
			sws_freeContext(scale);
			av_channel_layout_uninit(&sourceLayout);
			av_channel_layout_uninit(&targetLayout);
		}

		ReadResult failure(const std::wstring_view description, const int code)
		{
			error = std::wstring(description) + L": " + ffmpeg::error_text(code);
			return ReadResult::failed;
		}

		int configure_resample(const AVFrame& frame)
		{
			const auto channels = frame.ch_layout.nb_channels;
			if (channels < 1 || channels > 32 || frame.sample_rate < 1000 || frame.sample_rate > 384000 ||
			    frame.nb_samples <= 0 || frame.nb_samples > 2 * frame.sample_rate || !frame.extended_data)
				return AVERROR_INVALIDDATA;
			AVChannelLayout layout{};
			if (frame.ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&layout, channels);
			else if (const int result = av_channel_layout_copy(&layout, &frame.ch_layout); result < 0) return result;
			if (resample && frame.sample_rate == sourceRate && frame.format == sourceFormat &&
			    av_channel_layout_compare(&sourceLayout, &layout) == 0)
			{
				av_channel_layout_uninit(&layout);
				return 0;
			}
			swr_free(&resample);
			av_channel_layout_uninit(&sourceLayout);
			sourceLayout = layout;
			sourceRate = frame.sample_rate;
			sourceFormat = frame.format;
			haveAudioTime = false;
			const auto result = swr_alloc_set_opts2(&resample, &targetLayout, audio_format(target.format),
			                                        static_cast<int>(target.sampleRate), &sourceLayout,
			                                        static_cast<AVSampleFormat>(sourceFormat), sourceRate, 0, nullptr);
			return result < 0 ? result : swr_init(resample);
		}
	};

	SoftwareDecoder::SoftwareDecoder() : impl_(std::make_unique<Impl>())
	{
	}
	SoftwareDecoder::~SoftwareDecoder() = default;
	SoftwareDecoder::SoftwareDecoder(SoftwareDecoder&&) noexcept = default;
	SoftwareDecoder& SoftwareDecoder::operator=(SoftwareDecoder&&) noexcept = default;

	Info probe_software(const std::filesystem::path& path, const std::stop_token& stop)
	{
		Input input(path, stop);
		const int code = input.open();
		if (code < 0) return {.error = L"Could not read the media: " + ffmpeg::error_text(code)};
		auto info = media_info(*input.format);
		if (!info.hasVideo && !info.hasAudio) info.error = L"No audio or video track was found.";
		return info;
	}

	bool SoftwareDecoder::open(const std::filesystem::path& path, const std::stop_token& stop, const bool videoOnly)
	{
		impl_ = std::make_unique<Impl>();
		auto& d = *impl_;
		auto input = std::make_unique<Input>(path, stop);
		if (const int code = input->open(); code < 0)
		{
			d.failure(L"Could not read the media", code);
			return false;
		}
		d.info = media_info(*input->format);
		const auto start = time_in_ticks(input->format->start_time, ffmpeg::microTime);
		if (start != AV_NOPTS_VALUE) d.origin = start;
		const auto* video = primary(*input->format, AVMEDIA_TYPE_VIDEO, true);
		const auto* audio = videoOnly ? nullptr : primary(*input->format, AVMEDIA_TYPE_AUDIO, true);
		int failure = AVERROR_DECODER_NOT_FOUND;
		if (video && audio)
		{
			// Each pull cursor can read ahead of the other without accumulating packets from it.
			auto other = std::make_unique<Input>(input->file, stop);
			if ((failure = other->open()) >= 0)
				if (const auto* selected = primary(*other->format, AVMEDIA_TYPE_AUDIO, true))
				{
					auto track = std::make_unique<Track>();
					if ((failure = track->open(std::move(other), *selected)) >= 0) d.audio = std::move(track);
				}
		}
		if (const auto* selected = video ? video : audio)
		{
			auto track = std::make_unique<Track>();
			if ((failure = track->open(std::move(input), *selected)) >= 0)
				(video ? d.video : d.audio) = std::move(track);
		}
		if (d.video || d.audio) return true;
		d.failure(L"No supported track could be decoded", failure);
		return false;
	}

	const Info& SoftwareDecoder::info() const
	{
		return impl_->info;
	}
	bool SoftwareDecoder::has_video() const
	{
		return impl_->video != nullptr;
	}
	bool SoftwareDecoder::has_audio() const
	{
		return impl_->audio != nullptr;
	}
	const std::wstring& SoftwareDecoder::error() const
	{
		return impl_->error;
	}

	bool SoftwareDecoder::decode_audio(const AudioFormat& format)
	{
		auto& d = *impl_;
		if (!d.audio || format.sampleRate < 1000 || format.sampleRate > 384000 || !format.channels ||
		    format.channels > 32)
			return false;
		av_channel_layout_uninit(&d.targetLayout);
		if (!format.channelMask || av_channel_layout_from_mask(&d.targetLayout, format.channelMask) < 0 ||
		    d.targetLayout.nb_channels != static_cast<int>(format.channels))
		{
			av_channel_layout_uninit(&d.targetLayout);
			av_channel_layout_default(&d.targetLayout, static_cast<int>(format.channels));
		}
		swr_free(&d.resample);
		d.target = format;
		d.haveAudioTime = false;
		return true;
	}

	ReadResult SoftwareDecoder::read_video(VideoFrame& result, const ::core::sizei limit)
	{
		auto& d = *impl_;
		if (!d.video) return ReadResult::end;
		auto& track = *d.video;
		const int read = track.read();
		if (read == AVERROR_EOF) return ReadResult::end;
		if (read < 0) return d.failure(L"Video decoding stopped", read);
		const AVFrame& frame = *track.frame;
		if (!usable_size(frame.width, frame.height) || limit.cx <= 0 || limit.cy <= 0)
			return d.failure(L"The video picture exceeds the playback size limit", AVERROR(EINVAL));
		auto transform = ::core::simple_transform::none;
		if (const auto* matrix = av_frame_get_side_data(track.frame, AV_FRAME_DATA_DISPLAYMATRIX))
			transform = rotation(matrix->data, matrix->size);
		else if (const auto* trackMatrix = av_packet_side_data_get(track.stream->codecpar->coded_side_data,
		                                                           track.stream->codecpar->nb_coded_side_data,
		                                                           AV_PKT_DATA_DISPLAYMATRIX))
			transform = rotation(trackMatrix->data, trackMatrix->size);

		const bool quarter = transform == ::core::simple_transform::rot_90 ||
		                     transform == ::core::simple_transform::rot_270;
		const auto aspect = frame.sample_aspect_ratio.num > 0 && frame.sample_aspect_ratio.den > 0
		                        ? av_q2d(frame.sample_aspect_ratio)
		                        : 1.0;
		const auto squareWidth = frame.width * std::clamp(aspect, 0.125, 8.0);
		const auto ratio = (std::min)({1.0, (quarter ? limit.cy : limit.cx) / squareWidth,
		                               static_cast<double>(quarter ? limit.cx : limit.cy) / frame.height});
		const auto width = (std::max)(1, static_cast<int>(std::lround(squareWidth * ratio)));
		const auto height = (std::max)(1, static_cast<int>(std::lround(frame.height * ratio)));
		// BI_RGB's fourth byte is padding, not a transparency channel.
		const auto sampler = static_cast<AVPixelFormat>(frame.format);
		d.scale = sws_getCachedContext(d.scale, frame.width, frame.height, sampler, width, height, AV_PIX_FMT_BGRA,
		                               SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!d.scale) return d.failure(L"Cannot convert the video picture", AVERROR(ENOMEM));
		if (const auto* desc = av_pix_fmt_desc_get(sampler); desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB))
		{
			const auto* matrix = sws_getCoefficients(color_matrix(frame));
			sws_setColorspaceDetails(d.scale, matrix, frame.color_range == AVCOL_RANGE_JPEG, matrix, 1, 0, 1 << 16,
			                         1 << 16);
		}
		if (!result.picture) result.picture = std::make_shared<::core::surface>();
		auto& dest = transform == ::core::simple_transform::none ? *result.picture : d.staging;
		if (!dest.alloc({width, height}, ::core::texture_format::RGB))
			return d.failure(L"Cannot allocate the video picture", AVERROR(ENOMEM));
		std::uint8_t* rows[]{dest.pixels_line(0), nullptr, nullptr, nullptr};
		const int strides[]{static_cast<int>(dest.stride()), 0, 0, 0};
		if (sws_scale(d.scale, frame.data, frame.linesize, 0, frame.height, rows, strides) != height)
			return d.failure(L"Cannot convert the video pixels", AVERROR_INVALIDDATA);
		if (transform != ::core::simple_transform::none)
		{
			result.picture = dest.transform(transform);
			if (!result.picture) return d.failure(L"Cannot orient the video picture", AVERROR(ENOMEM));
		}
		result.duration = time_in_ticks(frame.duration, track.stream->time_base);
		if (result.duration <= 0)
		{
			const auto rate = track.stream->avg_frame_rate;
			result.duration = rate.num > 0 && rate.den > 0 ? av_rescale_q(1, av_inv_q(rate), tickTime)
			                                               : ticksPerSecond / 30;
		}
		result.time = track.time(d.origin);
		track.nextTime = result.time + (std::max)(result.duration, std::int64_t{1});
		return ReadResult::sample;
	}

	ReadResult SoftwareDecoder::read_audio(AudioBlock& block)
	{
		auto& d = *impl_;
		if (!d.audio || !d.target.sampleRate) return ReadResult::end;
		auto& track = *d.audio;
		block.samples.clear();
		for (;;)
		{
			const int read = track.read();
			if (read < 0 && read != AVERROR_EOF) return d.failure(L"Audio decoding stopped", read);
			const bool end = read == AVERROR_EOF;
			const AVFrame& frame = *track.frame;
			if (end && !d.resample) return ReadResult::end;
			if (!end)
				if (const int configured = d.configure_resample(frame); configured < 0)
					return d.failure(L"Cannot convert the audio format", configured);
			const auto delay = swr_get_delay(d.resample, ticksPerSecond);
			const auto time = end ? track.nextTime : track.time(d.origin) - delay;
			if (!d.haveAudioTime || std::abs(time - track.nextTime) > ticksPerSecond / 20) track.nextTime = time;
			d.haveAudioTime = true;
			const int capacity = swr_get_out_samples(d.resample, end ? 0 : frame.nb_samples);
			if (capacity < 0 || capacity > static_cast<int>(2 * d.target.sampleRate + 4096))
				return d.failure(L"The audio block exceeds the playback size limit", AVERROR(EINVAL));
			block.samples.resize(static_cast<size_t>(capacity) * d.target.frame_bytes());
			std::uint8_t* output = block.samples.data();
			std::array<const std::uint8_t*, 32> planes{};
			if (!end)
				std::copy_n(frame.extended_data,
				            av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame.format))
				                ? frame.ch_layout.nb_channels
				                : 1,
				            planes.begin());
			const int samples = swr_convert(d.resample, &output, capacity, end ? nullptr : planes.data(),
			                                end ? 0 : frame.nb_samples);
			if (samples < 0) return d.failure(L"Cannot resample audio", samples);
			block.samples.resize(static_cast<size_t>(samples) * d.target.frame_bytes());
			if (samples == 0)
			{
				if (end) return ReadResult::end;
				continue;
			}
			block.time = track.nextTime;
			track.nextTime += av_rescale_q(samples, {1, static_cast<int>(d.target.sampleRate)}, tickTime);
			return ReadResult::sample;
		}
	}

	bool SoftwareDecoder::seek(const std::int64_t time)
	{
		auto& d = *impl_;
		const auto target = std::clamp(time, std::int64_t{0}, d.info.hasDuration ? d.info.duration : maximumTime);
		const bool video = !d.video || d.video->seek(target, d.origin);
		const bool audio = !d.audio || d.audio->seek(target, d.origin);
		swr_free(&d.resample);
		d.haveAudioTime = false;
		return video && audio;
	}
}
