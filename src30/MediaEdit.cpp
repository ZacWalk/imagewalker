// ImageWalker by Zac Walker
// Copies compressed packets without decoding or re-encoding.

#include "MediaEdit.h"
#include "FfmpegInput.h"
#include "Media.h"

extern "C"
{
#include <libavutil/avstring.h>
#include <libavutil/display.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>
#include <limits>

namespace iw::media::edit
{
	namespace
	{
		using ffmpeg::Input;
		using ffmpeg::Io;
		using ffmpeg::microTime;
		using ffmpeg::picture;
		using ffmpeg::tickTime;

		struct Container
		{
			std::wstring_view extension;
			const char* demuxer;
			const char* muxer;
			bool quickTime{};
		};

		constexpr Container containers[] = {
			{L".mp4", "mov", "mp4", true},     {L".m4v", "mov", "mp4", true},  {L".3gp", "mov", "3gp", true},
			{L".3g2", "mov", "3g2", true},     {L".m4a", "mov", "ipod", true}, {L".mov", "mov", "mov", true},
			{L".mkv", "matroska", "matroska"}, {L".webm", "matroska", "webm"}, {L".avi", "avi", "avi"},
			{L".ts", "mpegts", "mpegts"},      {L".mts", "mpegts", "mpegts"},  {L".m2ts", "mpegts", "mpegts"},
			{L".wmv", "asf", "asf"},           {L".wma", "asf", "asf"},        {L".asf", "asf", "asf"},
			{L".flv", "flv", "flv"},           {L".mp3", "mp3", "mp3"},        {L".aac", "aac", "adts"},
			{L".flac", "flac", "flac"},        {L".ogg", "ogg", "ogg"},        {L".opus", "ogg", "opus"},
			{L".wav", "wav", "wav"},           {L".aiff", "aiff", "aiff"},     {L".aif", "aiff", "aiff"},
		};

		const Container* container_for(const std::wstring_view extension)
		{
			const auto i = std::ranges::find(containers, extension, &Container::extension);
			return i == std::end(containers) ? nullptr : i;
		}

		struct Output
		{
			Io io;
			Io readback;
			AVFormatContext* format{};

			Output(const files::StagedStream& stream, const std::stop_token& stop) :
				io{stream, stop}, readback{stream, stop}
			{
			}

			int open(const char* muxer)
			{
				const int result = avformat_alloc_output_context2(&format, nullptr, muxer, "staged-output");
				if (result < 0) return result;
				format->pb = io.open(true);
				if (!format->pb) return AVERROR(ENOMEM);
				format->flags |= AVFMT_FLAG_CUSTOM_IO;
				format->opaque = &readback;
				// MOV's fast-start pass can only re-read this same, still exclusively owned file.
				format->io_open =
					[](AVFormatContext* context, AVIOContext** opened, const char* url, const int flags, AVDictionary**)
				{
					if (flags != AVIO_FLAG_READ || !url || std::string_view(url) != context->url)
						return AVERROR(EACCES);
					auto& io = *static_cast<Io*>(context->opaque);
					io.offset = 0;
					*opened = io.open(false);
					return *opened ? 0 : AVERROR(ENOMEM);
				};
				format->io_close2 = [](AVFormatContext*, AVIOContext* context)
				{
					Io::close(context);
					return 0;
				};
				return 0;
			}

			~Output()
			{
				if (!format) return;
				Io::close(format->pb);
				avformat_free_context(format);
			}
		};

		std::wstring error_text(const int code)
		{
			if (code == AVERROR_EXIT) return L"Media editing cancelled. No file was published.";
			return ffmpeg::error_text(code);
		}

		bool can_mux(const AVFormatContext& input, const Container* container)
		{
			const auto* muxer = container ? av_guess_format(container->muxer, nullptr, nullptr) : nullptr;
			if (!muxer || input.nb_programs > 1) return false;
			for (unsigned i = 0; i < input.nb_streams; ++i)
			{
				const auto& stream = *input.streams[i]->codecpar;
				if (stream.codec_id == AV_CODEC_ID_NONE ||
				    avformat_query_codec(muxer, stream.codec_id, FF_COMPLIANCE_NORMAL) == 0)
					return false;
			}
			return true;
		}

		std::int64_t to_micro(const std::int64_t time, const AVStream& stream)
		{
			return time == AV_NOPTS_VALUE ? time : av_rescale_q(time, stream.time_base, microTime);
		}

		std::int64_t decode_time(const AVPacket& packet, const AVStream& stream)
		{
			return to_micro(packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts, stream);
		}

		bool turn_video(AVCodecParameters& codec, const int turn)
		{
			const auto* matrix = av_packet_side_data_get(codec.coded_side_data, codec.nb_coded_side_data,
			                                             AV_PKT_DATA_DISPLAYMATRIX);
			std::int32_t updated[9];
			if (!matrix) av_display_rotation_set(updated, turn * 90.0);
			else
			{
				if (matrix->size != sizeof(updated)) return false;
				std::memcpy(updated, matrix->data, sizeof(updated));
				for (int step = 0; step < turn; ++step)
					for (int row = 0; row < 9; row += 3)
					{
						if (updated[row + 1] == INT32_MIN) return false;
						const auto x = updated[row];
						updated[row] = -updated[row + 1];
						updated[row + 1] = x;
					}
			}
			auto* data = av_packet_side_data_new(&codec.coded_side_data, &codec.nb_coded_side_data,
			                                     AV_PKT_DATA_DISPLAYMATRIX, sizeof(updated), 0);
			if (!data) return false;
			std::memcpy(data->data, updated, sizeof(updated));
			return true;
		}

		bool copy_chapters(const AVFormatContext& input, AVFormatContext& output, const std::int64_t first,
		                   const std::int64_t last)
		{
			if (!input.nb_chapters) return true;
			output.chapters = static_cast<AVChapter**>(av_calloc(input.nb_chapters, sizeof(AVChapter*)));
			if (!output.chapters) return false;
			for (unsigned i = 0; i < input.nb_chapters; ++i)
			{
				const auto& in = *input.chapters[i];
				const auto begin = (std::max)(first, av_rescale_q(in.start, in.time_base, microTime));
				const auto end = (std::min)(last, av_rescale_q(in.end, in.time_base, microTime));
				if (begin >= end) continue;
				auto* out = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
				if (!out) return false;
				output.chapters[output.nb_chapters++] = out;
				out->id = in.id;
				out->time_base = microTime;
				out->start = av_sat_sub64(begin, first);
				out->end = av_sat_sub64(end, first);
				if (av_dict_copy(&out->metadata, in.metadata, 0) < 0) return false;
			}
			return true;
		}

		bool tracks_survived(const files::StagedStream& stream, const std::stop_token& stop,
		                     const std::vector<AVStream*>& written)
		{
			Input staged(stream, stop);
			if (staged.open() < 0) return false;
			std::vector<AVCodecID> observed, expected;
			for (unsigned i = 0; i < staged.format->nb_streams; ++i)
				observed.push_back(staged.format->streams[i]->codecpar->codec_id);
			for (const auto* track : written)
				if (track) expected.push_back(track->codecpar->codec_id);
			std::ranges::sort(observed);
			std::ranges::sort(expected);
			return observed == expected;
		}
	}

	Info inspect(const std::filesystem::path& localFile, const std::wstring_view extension, const std::stop_token& stop)
	{
		Input input(localFile, stop);
		Info info;
		if (const int code = input.open(); code < 0)
		{
			info.error = L"Cannot read media: " + error_text(code);
			return info;
		}
		const auto& format = *input.format;
		for (unsigned i = 0; i < format.nb_streams; ++i)
		{
			info.video |= picture(*format.streams[i]);
			info.audio |= format.streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
		}
		if (format.duration > 0) info.duration = av_rescale_q(format.duration, microTime, tickTime);
		const auto* same = container_for(extension);
		info.sameFormat = same && av_match_name(same->demuxer, format.iformat->name) && can_mux(format, same);
		info.mp4 = can_mux(format, container_for(L".mp4"));
		info.turn = info.video && ((info.sameFormat && same->quickTime) || info.mp4);
		if ((!info.video && !info.audio) || (!info.sameFormat && !info.mp4))
			info.error = L"No supported output container can keep this file's streams.";
		return info;
	}

	Result copy(const std::filesystem::path& localFile, const std::wstring_view extension, const Draft& draft,
	            const bool indexFirst, const files::StagedStream& stream, const std::stop_token& stop)
	{
		const auto fail = [](std::wstring message)
		{
			return Result{0, std::move(message)};
		};
		const auto* container = container_for(extension);
		if (!container || (draft.turn && !container->quickTime) || draft.turn < 0 || draft.turn > 3 ||
		    draft.first < 0 || draft.last < 0 || (draft.last && draft.first >= draft.last))
			return fail(L"These edits cannot be written in the chosen format or range.");

		Input input(localFile, stop);
		if (const int code = input.open(); code < 0) return fail(L"Cannot read media: " + error_text(code));
		auto& in = *input.format;
		if (in.nb_programs > 1) return fail(L"A copy cannot keep separate programs in this recording.");
		const auto origin = in.start_time == AV_NOPTS_VALUE ? 0 : in.start_time;
		auto first = av_sat_add64(origin, av_rescale_q(draft.first, tickTime, microTime));
		const auto last = draft.last ? av_sat_add64(origin, av_rescale_q(draft.last, tickTime, microTime)) : INT64_MAX;
		int video = -1;
		for (unsigned i = 0; i < in.nb_streams && video < 0; ++i)
			if (picture(*in.streams[i])) video = static_cast<int>(i);
		if (draft.turn && video < 0) return fail(L"There is no video track to rotate.");

		const auto freePacket = [](AVPacket* packet)
		{
			av_packet_free(&packet);
		};
		std::unique_ptr<AVPacket, decltype(freePacket)> packet(av_packet_alloc(), freePacket);
		if (!packet) return fail(L"There is not enough memory to copy this file.");
		auto firstVideoDts = first;
		if (video >= 0 && draft.first)
		{
			const auto& anchor = *in.streams[video];
			const auto target = av_rescale_q(first, microTime, anchor.time_base);
			if (av_seek_frame(&in, video, target, AVSEEK_FLAG_BACKWARD) < 0)
				return fail(L"Cannot find a keyframe near the requested trim start.");
			bool found = false;
			int code;
			while ((code = av_read_frame(&in, packet.get())) >= 0)
			{
				if (packet->stream_index == video && (packet->flags & AV_PKT_FLAG_KEY) &&
				    !(packet->flags & AV_PKT_FLAG_CORRUPT))
				{
					firstVideoDts = decode_time(*packet, anchor);
					first = packet->pts == AV_NOPTS_VALUE ? firstVideoDts : to_micro(packet->pts, anchor);
					found = firstVideoDts != AV_NOPTS_VALUE && first < last;
					break;
				}
				av_packet_unref(packet.get());
			}
			av_packet_unref(packet.get());
			if (!found)
				return fail(stop.stop_requested() ? error_text(AVERROR_EXIT) : L"No usable trim keyframe found.");
			if (av_seek_frame(&in, video, av_rescale_q(first, microTime, anchor.time_base), AVSEEK_FLAG_BACKWARD) < 0)
				return fail(L"Cannot return to the chosen keyframe.");
		}

		Output output(stream, stop);
		if (const int code = output.open(container->muxer); code < 0)
			return fail(L"Cannot open the staged media: " + error_text(code));
		auto& out = *output.format;
		std::vector<AVStream*> map(in.nb_streams);
		std::vector<bool> started(in.nb_streams);
		std::vector<std::int64_t> previous(in.nb_streams, AV_NOPTS_VALUE);
		for (unsigned i = 0; i < in.nb_streams; ++i)
		{
			const auto& from = *in.streams[i];
			if (draft.silent && from.codecpar->codec_type == AVMEDIA_TYPE_AUDIO) continue;
			if (from.codecpar->codec_id == AV_CODEC_ID_NONE ||
			    avformat_query_codec(out.oformat, from.codecpar->codec_id, FF_COMPLIANCE_NORMAL) == 0)
				return fail(L"The output cannot retain every stream. No file was published.");
			auto* dest = avformat_new_stream(&out, nullptr);
			if (!dest || avcodec_parameters_copy(dest->codecpar, from.codecpar) < 0 ||
			    av_dict_copy(&dest->metadata, from.metadata, 0) < 0)
				return fail(L"Cannot copy the stream parameters.");
			dest->codecpar->codec_tag = 0;
			dest->time_base = from.time_base;
			dest->avg_frame_rate = from.avg_frame_rate;
			dest->sample_aspect_ratio = from.sample_aspect_ratio;
			dest->disposition = from.disposition;
			if (draft.turn && picture(from) && !turn_video(*dest->codecpar, draft.turn))
				return fail(L"Cannot update this video track's rotation.");
			map[i] = dest;
		}
		if (av_dict_copy(&out.metadata, in.metadata, 0) < 0 || !copy_chapters(in, out, first, last))
			return fail(L"Cannot retain the media tags or chapters.");
		if (container->quickTime && av_opt_set(out.priv_data, "movflags",
		                                       indexFirst ? "faststart+use_metadata_tags" : "use_metadata_tags", 0) < 0)
			return fail(L"Cannot configure the MP4/QuickTime index and tags.");
		if ((extension == L".m2ts" || extension == L".mts") &&
		    av_opt_set(out.priv_data, "mpegts_m2ts_mode", "1", 0) < 0)
			return fail(L"Cannot configure the transport stream.");
		if (const int code = avformat_write_header(&out, nullptr); code < 0)
			return fail(L"The output container refused a stream: " + error_text(code));

		int code;
		bool wroteMedia = false;
		while ((code = av_read_frame(&in, packet.get())) >= 0)
		{
			const auto i = static_cast<unsigned>(packet->stream_index);
			if (i >= map.size()) return fail(L"A new stream appeared while reading the file.");
			if (packet->flags & AV_PKT_FLAG_CORRUPT) return fail(L"The input contains damaged packets.");
			if (auto* dest = map[i])
			{
				const auto& from = *in.streams[i];
				const auto dts = decode_time(*packet, from);
				const bool attached = (from.disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
				const auto start = i == static_cast<unsigned>(video) ? firstVideoDts : first;
				const bool wantsStart = draft.first && !attached;
				const bool inRange = attached || ((!wantsStart || dts >= start) && (!draft.last || dts <= last));
				const bool canStart = !wantsStart || started[i] || !picture(from) || (packet->flags & AV_PKT_FLAG_KEY);
				if (inRange && canStart)
				{
					started[i] = true;
					if (dts != AV_NOPTS_VALUE && previous[i] != AV_NOPTS_VALUE && dts < previous[i])
						return fail(L"The input timestamps restart mid-file. No file was published.");
					if (dts != AV_NOPTS_VALUE) previous[i] = dts;
					const auto offset = av_rescale_q(first, microTime, from.time_base);
					if (packet->pts != AV_NOPTS_VALUE) packet->pts = av_sat_sub64(packet->pts, offset);
					if (packet->dts != AV_NOPTS_VALUE) packet->dts = av_sat_sub64(packet->dts, offset);
					av_packet_rescale_ts(packet.get(), from.time_base, dest->time_base);
					packet->stream_index = dest->index;
					packet->pos = -1;
					if (const int written = av_interleaved_write_frame(&out, packet.get()); written < 0)
						return fail(L"Cannot copy a media packet: " + error_text(written));
					wroteMedia |= picture(from) || from.codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
				}
			}
			av_packet_unref(packet.get());
		}
		if (code != AVERROR_EOF || in.pb->error < 0)
			return fail(L"The source could not be read completely: " +
			            error_text(code != AVERROR_EOF ? code : in.pb->error));
		if (!wroteMedia) return fail(L"The chosen range contains no complete media packets.");
		if (stop.stop_requested()) return fail(error_text(AVERROR_EXIT));
		if (const int trailer = av_write_trailer(&out); trailer < 0)
			return fail(L"Cannot finish the media file: " + error_text(trailer));
		avio_flush(out.pb);
		if (out.pb->error < 0) return fail(L"Cannot flush the media file: " + error_text(out.pb->error));
		if (!tracks_survived(stream, stop, map))
			return fail(stop.stop_requested()
			                ? error_text(AVERROR_EXIT)
			                : L"The staged copy is unreadable or did not retain the expected tracks.");
		return {av_rescale_q(av_sat_sub64(first, origin), microTime, tickTime), {}};
	}
}
