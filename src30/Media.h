// ImageWalker by Zac Walker
// Native media probing, poster decoding and playback decoding; no network or downloaded codecs.

#pragma once

#include "Files.h"

#include <libdesktop/core.h>

#include <memory>

namespace iw::media
{
	inline constexpr std::int64_t ticksPerSecond = 10000000;

	struct Info
	{
		std::int64_t duration{};
		int width{};
		int height{};
		double frameRate{};
		std::wstring codec;
		std::wstring error;
		bool hasDuration{};
		bool hasVideo{};
		bool hasAudio{};
	};

	bool is_media(files::ItemKind kind);
	std::wstring duration_text(std::int64_t ticks);
	double frame_rate(std::uint32_t numerator, std::uint32_t denominator);
	Info probe(const std::filesystem::path& path, const std::stop_token& stop = {});
	files::DecodedImage poster(const std::filesystem::path& path, int maximumWidth, int maximumHeight,
	                           const std::stop_token& stop = {});

	enum class SampleFormat
	{
		float32,
		int16,
		int32
	};

	// Interleaved PCM: an audio output's own mix format.
	struct AudioFormat
	{
		std::uint32_t sampleRate{};
		std::uint32_t channels{};
		std::uint64_t channelMask{};
		SampleFormat format{SampleFormat::float32};

		std::uint32_t frame_bytes() const { return channels * (format == SampleFormat::int16 ? 2u : 4u); }
	};

	// A picture of opaque RGB rows, top down, shown from time for duration; times are 100-nanosecond ticks.
	struct VideoFrame
	{
		std::int64_t time{};
		std::int64_t duration{};
		::core::surface_ptr picture;
	};

	struct AudioBlock
	{
		std::int64_t time{};
		std::vector<std::uint8_t> samples;
	};

	enum class ReadResult
	{
		sample,
		end,
		failed
	};

	// Decodes one local file for playback on the thread that opened it: video as opaque RGB32 pictures with
	// square pixels, and audio as interleaved PCM in the format asked for. A seek lands at or before the time
	// asked for, so what comes before it is the caller's to skip.
	class Decoder
	{
	public:
		Decoder();
		~Decoder();
		Decoder(const Decoder&) = delete;
		Decoder& operator=(const Decoder&) = delete;

		bool open(const std::filesystem::path& path);
		// Converts the audio to format, resampling it; false leaves the file silent.
		bool decode_audio(const AudioFormat& format);
		const std::wstring& error() const;

		bool has_video() const;
		// The file has sound, decoded or not.
		bool has_audio() const;
		bool can_seek() const;
		std::optional<std::int64_t> duration() const;

		// frame.picture is written over when nothing else holds it and it is the right size.
		ReadResult read_video(VideoFrame& frame);
		ReadResult read_audio(AudioBlock& block);
		bool seek(std::int64_t time);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
