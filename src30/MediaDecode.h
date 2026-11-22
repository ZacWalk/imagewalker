// ImageWalker by Zac Walker
// Bundled FFmpeg playback decoder, independent of the window and audio device.

#pragma once

#include "Media.h"

namespace iw::media
{
	class SoftwareDecoder
	{
	public:
		SoftwareDecoder();
		~SoftwareDecoder();
		SoftwareDecoder(SoftwareDecoder&&) noexcept;
		SoftwareDecoder& operator=(SoftwareDecoder&&) noexcept;
		bool open(const std::filesystem::path& path, const std::stop_token& stop = {}, bool videoOnly = false);
		const Info& info() const;
		bool has_video() const;
		bool has_audio() const;
		bool decode_audio(const AudioFormat& format);
		ReadResult read_video(VideoFrame& frame, ::core::sizei limit = {1920, 1080});
		ReadResult read_audio(AudioBlock& block);
		bool seek(std::int64_t time);
		const std::wstring& error() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	Info probe_software(const std::filesystem::path& path, const std::stop_token& stop);
}
