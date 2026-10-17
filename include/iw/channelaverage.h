#pragma once

#include <cstdint>

namespace IW
{
	// Accumulators contain byte samples. Widen before rounding so even a full
	// 32-bit sum cannot overflow; malformed/empty sums never wrap a channel.
	constexpr std::uint8_t ChannelAverage(std::uint32_t sum, std::uint32_t count,
		bool round = false) noexcept
	{
		if (count == 0) return 0;
		const std::uint64_t value = (static_cast<std::uint64_t>(sum) + (round ? count / 2 : 0)) / count;
		return static_cast<std::uint8_t>(value > 255 ? 255 : value);
	}
}
