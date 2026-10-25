// ImageWalker by Zac Walker
// Native media probing and poster decoding; no network or downloaded codecs.

#pragma once

#include "Files.h"

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
}
