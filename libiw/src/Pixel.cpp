#include <libiw/Pixel.h>

#include <stdexcept>

namespace iw::imaging
{
	bool valid_pixels(const int width, const int height, const std::size_t count) noexcept
	{
		return width > 0 && height > 0 &&
			static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= count;
	}

	PixelPoint rotated_point(const int x, const int y, const int width, const int height,
	                         const int quarters) noexcept
	{
		switch ((quarters % 4 + 4) % 4)
		{
		case 1: return {height - 1 - y, x};
		case 2: return {width - 1 - x, height - 1 - y};
		case 3: return {y, width - 1 - x};
		default: return {x, y};
		}
	}

	PixelImage rotate_quarters(const PixelImage& source, int quarters)
	{
		if (!valid_pixels(source.width, source.height, source.pixels.size()))
			throw std::invalid_argument("Invalid image dimensions or pixel buffer.");
		quarters = (quarters % 4 + 4) % 4;
		if (!quarters) return source;
		const bool sideways = (quarters & 1) != 0;
		PixelImage result;
		result.width = sideways ? source.height : source.width;
		result.height = sideways ? source.width : source.height;
		result.originalWidth = sideways ? source.originalHeight : source.originalWidth;
		result.originalHeight = sideways ? source.originalWidth : source.originalHeight;
		result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
		for (int y = 0; y < source.height; ++y)
		{
			for (int x = 0; x < source.width; ++x)
			{
				const auto target = rotated_point(x, y, source.width, source.height, quarters);
				result.pixels[static_cast<std::size_t>(target.y) * result.width + target.x] =
					source.pixels[static_cast<std::size_t>(y) * source.width + x];
			}
		}
		return result;
	}
}
