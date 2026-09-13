// ImageWalker by Zac Walker
// Converts layout metrics between 96-DPI logical units and monitor-specific physical pixels.

#pragma once

#include <cstdint>

namespace iw::ui
{
	inline constexpr unsigned int default_dpi = 96;

	inline int rounded_divide(const std::int64_t numerator, const std::int64_t denominator)
	{
		return static_cast<int>((numerator + (numerator < 0 ? -denominator / 2 : denominator / 2)) /
			denominator);
	}

	inline int scale_metric(const int logicalValue, const unsigned int dpi)
	{
		return rounded_divide(static_cast<std::int64_t>(logicalValue) * (dpi ? dpi : default_dpi), default_dpi);
	}

	inline int unscale_metric(const int physicalValue, const unsigned int dpi)
	{
		return rounded_divide(static_cast<std::int64_t>(physicalValue) * default_dpi,
		                      dpi ? dpi : default_dpi);
	}
}
