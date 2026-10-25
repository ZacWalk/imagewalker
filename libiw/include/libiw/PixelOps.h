// ImageWalker by Zac Walker
// Declares runtime-dispatched BGRA pixel operations used by the software renderer.

#pragma once

#include <libiw/Color.h>

#include <cstdint>
#include <span>

namespace iw::imaging
{
	bool has_sse2() noexcept;
	bool has_avx2() noexcept;

	enum class PixelBackend { automatic, scalar, sse2, avx2 };

	void blend_constant_bgra(std::span<std::uint32_t> pixels, color foreground,
	                         PixelBackend backend = PixelBackend::automatic);
	void interpolate_bgra(std::span<const std::uint32_t> first, std::span<const std::uint32_t> second,
	                      std::span<std::uint32_t> destination, std::uint16_t fraction,
	                      PixelBackend backend = PixelBackend::automatic);
}
