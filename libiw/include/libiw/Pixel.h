#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace iw::imaging
{
	struct PixelImage
	{
		int width{};
		int height{};
		std::vector<std::uint32_t> pixels;
		int originalWidth{};
		int originalHeight{};
	};

	struct PixelPoint
	{
		int x{};
		int y{};
	};

	constexpr std::uint32_t unpack_bgr555(const std::uint16_t value) noexcept
	{
		return ((value & 0x001f) << 3) | ((value & 0x03e0) << 6) | ((value & 0x7c00) << 9);
	}

	bool valid_pixels(int width, int height, std::size_t count) noexcept;
	PixelPoint rotated_point(int x, int y, int width, int height, int quarters) noexcept;
	PixelImage rotate_quarters(const PixelImage& source, int quarters);
}
