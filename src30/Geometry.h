// ImageWalker by Zac Walker
// Thin x/y/width/height application models over libdesktop's shared geometry.

#pragma once

#include <libdesktop/system.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace iw::app
{
	struct sizei
	{
		int width{};
		int height{};

		constexpr sizei() = default;
		constexpr sizei(int w, int h) : width(w), height(h) {}
		constexpr sizei(::sizei value) : width(value.cx), height(value.cy) {}
		constexpr operator ::sizei() const { return {width, height}; }
		constexpr bool operator==(const sizei&) const = default;
		constexpr bool is_empty() const { return width <= 0 || height <= 0; }
		constexpr int area() const { return width * height; }
		constexpr sizei operator+(const sizei value) const { return {width + value.width, height + value.height}; }
		constexpr sizei operator-(const sizei value) const { return {width - value.width, height - value.height}; }
		constexpr sizei operator/(int value) const { return {width / value, height / value}; }
		constexpr sizei operator*(int value) const { return {width * value, height * value}; }
		sizei operator*(double value) const
		{
			return {static_cast<int>(std::lround(width * value)),
			        static_cast<int>(std::lround(height * value))};
		}
		constexpr sizei inflate(int value) const { return {width + value, height + value}; }
		constexpr sizei flip() const { return {height, width}; }
	};

	struct pointi
	{
		int x{};
		int y{};

		constexpr pointi() = default;
		constexpr pointi(int xValue, int yValue) : x(xValue), y(yValue) {}
		constexpr pointi(::pointi value) : x(value.x), y(value.y) {}
		constexpr operator ::pointi() const { return {x, y}; }
		constexpr bool operator==(const pointi&) const = default;
		constexpr pointi operator+(const sizei value) const { return {x + value.width, y + value.height}; }
		constexpr pointi operator-(const sizei value) const { return {x - value.width, y - value.height}; }
		constexpr pointi operator+(const pointi value) const { return {x + value.x, y + value.y}; }
		constexpr pointi operator-(const pointi value) const { return {x - value.x, y - value.y}; }
		constexpr pointi operator-() const { return {-x, -y}; }
	};

	struct recti
	{
		int x{};
		int y{};
		int width{};
		int height{};

		constexpr recti() = default;
		constexpr recti(int xValue, int yValue, int w, int h)
			: x(xValue), y(yValue), width(w), height(h) {}
		constexpr recti(sizei value) : width(value.width), height(value.height) {}
		constexpr recti(pointi location, sizei extent)
			: x(location.x), y(location.y), width(extent.width), height(extent.height) {}
		constexpr recti(::recti value)
			: x(value.left), y(value.top), width(value.width()), height(value.height()) {}
		constexpr operator ::recti() const { return {x, y, x + width, y + height}; }

		constexpr int right() const { return x + width; }
		constexpr int bottom() const { return y + height; }
		constexpr int area() const { return width * height; }
		constexpr sizei extent() const { return {width, height}; }
		constexpr pointi top_left() const { return {x, y}; }
		constexpr pointi top_center() const { return {x + width / 2, y}; }
		constexpr pointi bottom_center() const { return {x + width / 2, bottom()}; }
		constexpr pointi bottom_right() const { return {right(), bottom()}; }
		constexpr pointi center() const { return {x + width / 2, y + height / 2}; }
		constexpr bool is_empty() const { return width <= 0 || height <= 0; }
		constexpr bool is_null() const { return x == 0 && y == 0 && width == 0 && height == 0; }
		constexpr bool contains(pointi value) const
		{
			return value.x >= x && value.x < right() && value.y >= y && value.y < bottom();
		}
		constexpr bool intersects(recti value) const
		{
			return x < value.right() && right() > value.x && y < value.bottom() && bottom() > value.y;
		}
		constexpr recti intersection(recti value) const
		{
			const auto result = static_cast<::recti>(*this).intersection(static_cast<::recti>(value));
			return recti(result);
		}
		constexpr recti make_union(recti value) const
		{
			const auto result = static_cast<::recti>(*this).make_union(static_cast<::recti>(value));
			return recti(result);
		}
		constexpr recti inflate(int value) const
		{
			return {x - value, y - value, width + value * 2, height + value * 2};
		}
		constexpr recti inflate(int horizontal, int vertical) const
		{
			return {x - horizontal, y - vertical, width + horizontal * 2, height + vertical * 2};
		}
		constexpr recti extend(int horizontal, int vertical) const
		{
			return {x, y, width + horizontal, height + vertical};
		}
		constexpr recti offset(pointi value) const { return {x + value.x, y + value.y, width, height}; }
		constexpr recti offset(int horizontal, int vertical) const
		{
			return {x + horizontal, y + vertical, width, height};
		}
		constexpr recti crop(recti value) const
		{
			return intersection(value);
		}
		constexpr recti& operator=(const recti&) = default;
		constexpr bool operator==(const recti&) const = default;
	};

	inline constexpr double to_radian(const double value) noexcept
	{
		return ::to_radian(value);
	}

	inline constexpr double to_degrees(const double value) noexcept
	{
		return ::to_degrees(value);
	}
}
