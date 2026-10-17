// This file is part of the ImageWalker photo and video organizer
// Copyright Zac Walker
//
// Purpose: ImageWalker 3.0 color primitives shared by its rendering and image code.

#pragma once

#include <cstdint>

namespace iw
{
	class color
	{
	public:
		std::uint8_t r{};
		std::uint8_t g{};
		std::uint8_t b{};
		std::uint8_t a{255};

		constexpr color() noexcept = default;

		constexpr color(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue,
		                const std::uint8_t alpha = 255) noexcept
			: r(red), g(green), b(blue), a(alpha)
		{
		}

		constexpr std::uint32_t pack() const noexcept
		{
			return r | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16);
		}

		static constexpr color from_packed(const std::uint32_t value, const std::uint8_t alpha = 255) noexcept
		{
			return {
				static_cast<std::uint8_t>(value),
				static_cast<std::uint8_t>(value >> 8),
				static_cast<std::uint8_t>(value >> 16),
				alpha
			};
		}

		constexpr color with_alpha(const std::uint8_t alpha) const noexcept
		{
			return {r, g, b, alpha};
		}

		constexpr bool operator==(const color&) const noexcept = default;
	};

	namespace colors
	{
		inline constexpr color black{0, 0, 0};
		inline constexpr color dark_gray{55, 55, 55};
		inline constexpr color light_gray{238, 238, 238};
	}
}
