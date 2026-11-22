// ImageWalker by Zac Walker
// Local, cancellable AVIO shared by playback and packet-copy editing.

#pragma once

#include "DesktopServices.h"
#include "Media.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <array>
#include <utility>

namespace iw::media::ffmpeg
{
	inline constexpr AVRational tickTime{1, static_cast<int>(ticksPerSecond)};
	inline constexpr AVRational microTime{1, AV_TIME_BASE};
	inline constexpr std::int64_t maxVideoPixels = 36 * 1024 * 1024;
	inline constexpr unsigned streamLimit = 64;

	inline bool picture(const AVStream& stream)
	{
		return stream.codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		       !(stream.disposition & (AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_TIMED_THUMBNAILS));
	}

	inline std::wstring error_text(const int code)
	{
		char message[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror(code, message, sizeof(message));
		return {message, message + std::char_traits<char>::length(message)};
	}

	struct Io
	{
		const files::StagedStream& stream;
		std::stop_token stop;
		std::int64_t offset{};

		static int read(void* opaque, std::uint8_t* bytes, const int size)
		{
			auto& self = *static_cast<Io*>(opaque);
			if (self.stop.stop_requested()) return AVERROR_EXIT;
			const auto end = self.stream.size();
			if (end < 0) return AVERROR(EIO);
			if (self.offset >= end) return AVERROR_EOF;
			const auto n = static_cast<int>((std::min)(std::int64_t{size}, end - self.offset));
			if (!self.stream.read(self.offset, {bytes, static_cast<size_t>(n)})) return AVERROR(EIO);
			self.offset += n;
			return n;
		}

		static int write(void* opaque, const std::uint8_t* bytes, const int size)
		{
			auto& self = *static_cast<Io*>(opaque);
			if (self.stop.stop_requested()) return AVERROR_EXIT;
			if (self.offset > INT64_MAX - size || !self.stream.write(self.offset, {bytes, static_cast<size_t>(size)}))
				return AVERROR(EIO);
			self.offset += size;
			return size;
		}

		static std::int64_t seek(void* opaque, const std::int64_t position, const int whence)
		{
			auto& self = *static_cast<Io*>(opaque);
			if (self.stop.stop_requested()) return AVERROR_EXIT;
			const auto origin = whence & ~AVSEEK_FORCE;
			if (origin == AVSEEK_SIZE) return self.stream.size();
			const auto base = origin == SEEK_SET   ? 0
			                  : origin == SEEK_CUR ? self.offset
			                  : origin == SEEK_END ? self.stream.size()
			                                       : -1;
			if (base < 0 || position < -base || position > INT64_MAX - base) return AVERROR(EINVAL);
			return self.offset = base + position;
		}

		AVIOContext* open(const bool writable)
		{
			constexpr int bufferSize = 128 * 1024;
			auto* buffer = static_cast<std::uint8_t*>(av_malloc(bufferSize));
			if (!buffer) return nullptr;
			auto* context = avio_alloc_context(buffer, bufferSize, writable, this, writable ? nullptr : read,
			                                   writable ? write : nullptr, seek);
			if (!context) av_free(buffer);
			return context;
		}

		static void close(AVIOContext* context)
		{
			if (!context) return;
			av_freep(&context->buffer);
			avio_context_free(&context);
		}
	};

	struct Input
	{
		::platform::file_ptr file;
		files::StagedStream reader{[this]
		{
			const auto size = file ? file->size() : UINT64_MAX;
			return size <= INT64_MAX ? static_cast<std::int64_t>(size) : -1;
		},
		                           [this](const std::int64_t offset, const std::span<std::uint8_t> bytes)
		{
			return file &&
			       file->read_at(static_cast<std::uint64_t>(offset), bytes.data(), bytes.size()) == bytes.size();
		},
		                           {}};
		Io io;
		AVIOContext* buffer{};
		AVFormatContext* format{};

		Input(const std::filesystem::path& path, const std::stop_token& stop) :
			Input(::platform::open_file_lock(desktop::file_path(path), ::platform::file_lock_mode::read), stop)
		{
		}
		Input(::platform::file_ptr source, const std::stop_token& stop) : file(std::move(source)), io{reader, stop} {}
		Input(const files::StagedStream& stream, const std::stop_token& stop) : io{stream, stop} {}
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;

		int open()
		{
			if (io.stop.stop_requested()) return AVERROR_EXIT;
			if (io.stream.size() <= 0) return AVERROR(EIO);
			buffer = io.open(false);
			format = avformat_alloc_context();
			if (!buffer || !format) return AVERROR(ENOMEM);
			format->pb = buffer;
			format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_GENPTS;
			format->max_streams = streamLimit;
			format->interrupt_callback = {[](void* context) -> int
			{ return static_cast<Io*>(context)->stop.stop_requested(); }, &io};
			format->io_open = [](AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**)
			{
				return AVERROR(EACCES);
			};
			const int opened = avformat_open_input(&format, nullptr, nullptr, nullptr);
			if (opened < 0) return opened;
			// Newly discovered tracks get no per-stream decoder options from FFmpeg.
			if (format->ctx_flags & AVFMTCTX_NOHEADER)
			{
				format->codec_whitelist = av_strdup("ac3,eac3,dca,vorbis,opus,mp2,mp3,aac,aac_latm,flac");
				if (!format->codec_whitelist) return AVERROR(ENOMEM);
			}
			if (format->nb_streams > streamLimit) return AVERROR_INVALIDDATA;
			std::array<AVDictionary*, streamLimit> options{};
			for (unsigned i = 0; i < format->nb_streams; ++i)
			{
				if (av_dict_set_int(&options[i], "max_pixels", maxVideoPixels, 0) < 0 ||
				    av_dict_set_int(&options[i], "max_samples", 2 * 1024 * 1024, 0) < 0)
				{
					for (auto& option : options) av_dict_free(&option);
					return AVERROR(ENOMEM);
				}
			}
			const int result = avformat_find_stream_info(format, options.data());
			for (auto& option : options) av_dict_free(&option);
			av_freep(&format->codec_whitelist);
			return result;
		}

		~Input()
		{
			if (format) avformat_close_input(&format);
			Io::close(buffer);
		}
	};
}
