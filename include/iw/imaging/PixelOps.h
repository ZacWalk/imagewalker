// ImageWalker by Zac Walker
// Declares runtime-dispatched BGRA pixel operations used by the software renderer.

#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <iw/imaging/Color.h>
#include <libdesktop/core.h>

#include <cstdint>
#include <span>

namespace iw::imaging
{
	inline bool has_sse2() noexcept { return ::core::has_sse2(); }
	inline bool has_avx2() noexcept { return ::core::has_avx2(); }

	using PixelBackend = ::core::pixel_backend;

	inline void blend_constant_bgra(const std::span<std::uint32_t> pixels, const color foreground,
		const PixelBackend backend = PixelBackend::automatic)
	{
		::core::blend_constant_bgra(pixels, ::core::rgba(foreground.r, foreground.g, foreground.b, foreground.a), backend);
	}

	inline void interpolate_bgra(const std::span<const std::uint32_t> first,
		const std::span<const std::uint32_t> second, const std::span<std::uint32_t> destination,
		const std::uint16_t fraction, const PixelBackend backend = PixelBackend::automatic)
	{
		::core::interpolate_bgra(first, second, destination, fraction, backend);
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
