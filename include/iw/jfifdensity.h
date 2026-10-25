#pragma once

#include <cstdint>

namespace IW
{
	constexpr std::uint16_t JfifDensity(std::uint32_t pelsPerMeter, bool centimeters = false) noexcept
	{
		const std::uint64_t divisor = centimeters ? 100 : 3937;
		const std::uint64_t scaled = static_cast<std::uint64_t>(pelsPerMeter) * (centimeters ? 1 : 100);
		const std::uint64_t density = (scaled + divisor / 2) / divisor;
		return static_cast<std::uint16_t>(density < 1 ? 1 : density > 65535 ? 65535 : density);
	}
}
