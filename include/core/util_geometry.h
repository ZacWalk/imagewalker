// This file is part of the ImagewWlker photo and video organizer
// Copyright Zac Walker
// 
// Purpose: Shared geometry primitives and math. Defines point, size, rectangle, and quad
// types with transformation, intersection, and scaling operations.

#pragma once

#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// windows.h has min and max as macros unless NOMINMAX is set, and the WTL trees
// rely on them throughout. Suppressed here rather than at every include site:
// this is the header that declares members of those names and calls std::min.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

namespace iw
{
	using real = double;

	class sizei;
	class pointi;
	class recti;
	class sized;
	class pointd;
	class rectd;

	constexpr double geom_pi = 3.14159265358979323846;

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	constexpr bool is_equal(const double l, const double r)
	{
		return equiv(l, r);
	}

	constexpr double to_radian(const double theta) noexcept
	{
		return theta * (geom_pi / 180.0);
	}

	constexpr double to_degrees(const double theta) noexcept
	{
		return theta / (geom_pi / 180.0);
	}

	class sizei
	{
	public:
		int width = 0;
		int height = 0;

		constexpr sizei() noexcept = default;
		constexpr sizei(const sizei& other) noexcept = default;

		constexpr sizei(const int width, const int height) noexcept : width(width), height(height)
		{
		}

		explicit constexpr sizei(pointi other) noexcept;

		constexpr bool operator ==(const sizei other) const noexcept
		{
			return width == other.width && height == other.height;
		}

		constexpr bool operator !=(const sizei other) const noexcept
		{
			return !(*this == other);
		}

		constexpr bool is_empty() const noexcept
		{
			return width == 0 || height == 0;
		}

		constexpr sizei operator +(const sizei other) const noexcept
		{
			return {width + other.width, height + other.height};
		}

		constexpr sizei operator -(const sizei other) const noexcept
		{
			return {width - other.width, height - other.height};
		}

		constexpr sizei operator -() const noexcept
		{
			return {-width, -height};
		}

		constexpr sizei operator /(const int divisor) const noexcept
		{
			return {width / divisor, height / divisor};
		}

		constexpr sizei operator *(const int multiplier) const noexcept
		{
			return {width * multiplier, height * multiplier};
		}

		constexpr sizei operator *(const double multiplier) const noexcept
		{
			return {round(width * multiplier), round(height * multiplier)};
		}

		constexpr sizei inflate(const int amount) const noexcept
		{
			return {width + amount, height + amount};
		}

		constexpr sizei flip() const noexcept
		{
			return {height, width};
		}

		constexpr int area() const noexcept
		{
			return width * height;
		}

		constexpr pointi operator +(pointi value) const noexcept;
		constexpr pointi operator -(pointi value) const noexcept;
	};

	class pointi
	{
	public:
		int x = 0;
		int y = 0;

		constexpr pointi() noexcept = default;
		constexpr pointi(const pointi& other) noexcept = default;

		constexpr pointi(const int x, const int y) noexcept : x(x), y(y)
		{
		}

		constexpr pointi(const sizei other) noexcept : x(other.width), y(other.height)
		{
		}

		constexpr bool operator ==(const pointi other) const noexcept
		{
			return x == other.x && y == other.y;
		}

		constexpr bool operator !=(const pointi other) const noexcept
		{
			return x != other.x || y != other.y;
		}

		constexpr pointi operator +(const sizei other) const noexcept
		{
			return {x + other.width, y + other.height};
		}

		constexpr pointi operator -(const sizei other) const noexcept
		{
			return {x - other.width, y - other.height};
		}

		constexpr pointi operator -() const noexcept
		{
			return {-x, -y};
		}

		constexpr pointi operator +(const pointi other) const noexcept
		{
			return {x + other.x, y + other.y};
		}

		constexpr pointi operator -(const pointi other) const noexcept
		{
			return {x - other.x, y - other.y};
		}

		constexpr pointi clamp(recti limit) const noexcept;

		int dist_sqrd(const pointi other) const
		{
			const auto dx = x - other.x;
			const auto dy = y - other.y;
			return round(std::sqrt(dx * dx + dy * dy));
		}
	};

	class recti
	{
	public:
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;

		constexpr recti() noexcept = default;

		constexpr recti(const int x, const int y, const int width, const int height) noexcept :
			x(x), y(y), width(width), height(height)
		{
		}

		constexpr recti(const int width, const int height) noexcept : width(width), height(height)
		{
		}

		constexpr recti(const sizei extent) noexcept : width(extent.width), height(extent.height)
		{
		}

		constexpr recti(const pointi location, const sizei extent) noexcept : x(location.x), y(location.y),
		                                                                      width(extent.width), height(extent.height)
		{
		}

		constexpr recti(const pointi topLeft, const pointi bottomRight) noexcept : x(topLeft.x), y(topLeft.y),
			width(bottomRight.x - topLeft.x), height(bottomRight.y - topLeft.y)
		{
		}

		constexpr int right() const noexcept
		{
			return x + width;
		}

		constexpr int bottom() const noexcept
		{
			return y + height;
		}

		constexpr int area() const noexcept
		{
			return width * height;
		}

		constexpr sizei extent() const noexcept
		{
			return {width, height};
		}

		constexpr pointi top_left() const noexcept
		{
			return {x, y};
		}

		constexpr pointi top_center() const noexcept
		{
			return {x + width / 2, y};
		}

		constexpr pointi bottom_center() const noexcept
		{
			return {x + width / 2, bottom()};
		}

		constexpr pointi bottom_right() const noexcept
		{
			return {right(), bottom()};
		}

		constexpr pointi center() const noexcept
		{
			return {x + width / 2, y + height / 2};
		}

		constexpr recti operator+(const sizei offset) const noexcept
		{
			return {x + offset.width, y + offset.height, width, height};
		}

		constexpr bool is_empty() const noexcept
		{
			return width <= 0 || height <= 0;
		}

		constexpr bool is_null() const noexcept
		{
			return x == 0 && y == 0 && width == 0 && height == 0;
		}

		constexpr bool contains(const pointi value) const noexcept
		{
			return value.x >= x && value.x < right() && value.y >= y && value.y < bottom();
		}

		constexpr void clear() noexcept
		{
			x = y = width = height = 0;
		}

		constexpr void set(const int x1, const int y1, const int x2, const int y2) noexcept
		{
			x = x1;
			y = y1;
			width = x2 - x1;
			height = y2 - y1;
		}

		constexpr void set(const pointi topLeft, const pointi bottomRight) noexcept
		{
			set(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
		}

		constexpr recti inflate(const int xy) const noexcept
		{
			return {x - xy, y - xy, width + xy * 2, height + xy * 2};
		}

		constexpr recti inflate(const int horizontal, const int vertical) const noexcept
		{
			return {x - horizontal, y - vertical, width + horizontal * 2, height + vertical * 2};
		}

		constexpr recti inflate(const int left, const int top, const int right, const int bottom) const noexcept
		{
			return {x - left, y - top, width + left + right, height + top + bottom};
		}

		constexpr recti inflate(const sizei amount) const noexcept
		{
			return inflate(amount.width, amount.height);
		}

		constexpr recti extend(const int horizontal, const int vertical) const noexcept
		{
			return {x, y, width + horizontal, height + vertical};
		}

		constexpr bool intersects(const recti other) const noexcept
		{
			return x < other.right() && right() > other.x && y < other.bottom() && bottom() > other.y;
		}

		constexpr recti intersection(const recti other) const noexcept
		{
			if (!intersects(other)) return {};
			const int resultX = std::max(x, other.x);
			const int resultY = std::max(y, other.y);
			return {
				resultX, resultY, std::min(right(), other.right()) - resultX,
				std::min(bottom(), other.bottom()) - resultY
			};
		}

		constexpr recti make_union(const recti other) const noexcept
		{
			if (is_empty()) return other;
			if (other.is_empty()) return *this;
			const int resultX = std::min(x, other.x);
			const int resultY = std::min(y, other.y);
			return {
				resultX, resultY, std::max(right(), other.right()) - resultX,
				std::max(bottom(), other.bottom()) - resultY
			};
		}

		constexpr recti clamp(const recti limit) const noexcept
		{
			sizei adjustment;

			if (y < limit.y)
				adjustment.height = limit.y - y;

			if (x < limit.x)
				adjustment.width = limit.x - x;

			if (bottom() > limit.bottom())
				adjustment.height = limit.bottom() - bottom();

			if (right() > limit.right())
				adjustment.width = limit.right() - right();

			return *this + adjustment;
		}

		constexpr recti crop(const recti limit) const noexcept
		{
			const int resultX = std::max(x, limit.x);
			const int resultY = std::max(y, limit.y);
			return {
				resultX, resultY, std::max(0, std::min(right(), limit.right()) - resultX),
				std::max(0, std::min(bottom(), limit.bottom()) - resultY)
			};
		}

		constexpr recti offset(const pointi value) const noexcept
		{
			return {x + value.x, y + value.y, width, height};
		}

		constexpr recti offset(const int horizontal, const int vertical) const noexcept
		{
			return {x + horizontal, y + vertical, width, height};
		}

		constexpr recti& operator =(const recti& other) noexcept = default;

		constexpr bool operator ==(const recti other) const noexcept
		{
			return x == other.x && y == other.y && width == other.width && height == other.height;
		}

		constexpr bool operator !=(const recti other) const noexcept
		{
			return !(*this == other);
		}

		constexpr recti normalise() const
		{
			return {std::min(x, right()), std::min(y, bottom()), std::abs(width), std::abs(height)};
		}

		constexpr void exclude(const pointi location, const recti bounds)
		{
			if (intersects(bounds))
			{
				if (bounds.right() < location.x)
				{
					const int newX = std::max(x, bounds.right());
					width = right() - newX;
					x = newX;
				}

				if (bounds.x > location.x)
				{
					width = std::min(right(), bounds.x) - x;
				}

				if (bounds.bottom() < location.y)
				{
					const int newY = std::max(y, bounds.bottom());
					height = bottom() - newY;
					y = newY;
				}

				if (bounds.y > location.y)
				{
					height = std::min(bottom(), bounds.y) - y;
				}
			}
		}
	};

	constexpr int64_t distance_squared(const pointi value, const recti bounds) noexcept
	{
		const auto dx = value.x < bounds.x
			                ? static_cast<int64_t>(bounds.x) - value.x
			                : value.x > bounds.right()
			                ? static_cast<int64_t>(value.x) - bounds.right()
			                : 0;
		const auto dy = value.y < bounds.y
			                ? static_cast<int64_t>(bounds.y) - value.y
			                : value.y > bounds.bottom()
			                ? static_cast<int64_t>(value.y) - bounds.bottom()
			                : 0;
		return dx * dx + dy * dy;
	}

	constexpr sizei::sizei(const pointi other) noexcept : width(other.x), height(other.y)
	{
	}

	constexpr pointi sizei::operator +(const pointi value) const noexcept
	{
		return {width + value.x, height + value.y};
	}

	constexpr pointi sizei::operator -(const pointi value) const noexcept
	{
		return {width - value.x, height - value.y};
	}

	constexpr pointi pointi::clamp(const recti limit) const noexcept
	{
		return {std::clamp(x, limit.x, limit.right()), std::clamp(y, limit.y, limit.bottom())};
	}

	constexpr recti center_rect(const sizei extent, const int centerX, const int centerY) noexcept
	{
		return {centerX - extent.width / 2, centerY - extent.height / 2, extent.width, extent.height};
	}

	constexpr recti center_rect(const sizei extent, const recti limit) noexcept
	{
		const auto center = limit.center();
		return center_rect(extent, center.x, center.y);
	}

	constexpr recti center_rect(const sizei extent, const sizei limit) noexcept
	{
		return center_rect(extent, limit.width / 2, limit.height / 2);
	}

	constexpr recti center_rect(const sizei extent, const pointi limit) noexcept
	{
		return center_rect(extent, limit.x, limit.y);
	}

	constexpr recti center_rect(const recti value, const recti limit) noexcept
	{
		return center_rect(value.extent(), limit);
	}

	constexpr recti round_rect(const double x, const double y, const double width, const double height) noexcept
	{
		return {round(x), round(y), round(width), round(height)};
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class sized
	{
	public:
		constexpr sized() noexcept = default;
		constexpr sized(const sized& other) noexcept = default;
		constexpr sized& operator=(const sized& other) noexcept = default;

		constexpr sized(const double w, const double h) noexcept : Width(w), Height(h)
		{
		}

		constexpr sized(const sizei other) noexcept : Width(other.width), Height(other.height)
		{
		}

		constexpr explicit sized(const pointi other) noexcept : Width(other.x), Height(other.y)
		{
		}

		constexpr sized(pointd other) noexcept;

		constexpr sized operator+(const sized other) const noexcept
		{
			return {
				Width + other.Width,
				Height + other.Height
			};
		}

		constexpr sized operator-(const sized other) const noexcept
		{
			return {Width - other.Width, Height - other.Height};
		}

		constexpr sized operator/(const sized other) const noexcept
		{
			return {Width / other.Width, Height / other.Height};
		}

		constexpr sized operator/(const double other) const noexcept
		{
			return {Width / other, Height / other};
		}

		constexpr sized operator*(const sized other) const noexcept
		{
			return {Width * other.Width, Height * other.Height};
		}

		constexpr sized operator*(const double other) const noexcept
		{
			return {Width * other, Height * other};
		}

		constexpr bool operator==(const sized other) const noexcept
		{
			return equals(other);
		}

		constexpr bool operator!=(const sized other) const noexcept
		{
			return !equals(other);
		}

		constexpr bool equals(const sized other) const noexcept
		{
			return is_equal(Width, other.Width) && is_equal(Height, other.Height);
		}

		constexpr bool is_empty() const noexcept
		{
			return Width == 0.0 && Height == 0.0;
		}

		constexpr sizei round() const noexcept
		{
			return {iw::round(Width), iw::round(Height)};
		}

		constexpr sizei ceil() const noexcept
		{
			return {static_cast<int>(std::ceil(Width)), static_cast<int>(std::ceil(Height))};
		}

		double Width = 0.0;
		double Height = 0.0;
	};

	class pointd
	{
	public:
		constexpr pointd() noexcept = default;
		constexpr pointd(const pointd& point) noexcept = default;

		constexpr pointd(const double x, const double y) noexcept : X(x), Y(y)
		{
		}

		constexpr pointd(const pointi loc) noexcept : X(loc.x), Y(loc.y)
		{
		}

		constexpr pointd& operator=(const pointd& other) noexcept = default;

		constexpr bool operator==(const pointd other) const noexcept
		{
			return equals(other);
		}

		constexpr bool operator!=(const pointd other) const noexcept
		{
			return !equals(other);
		}

		double dist(const pointd other) const noexcept
		{
			const auto x = other.X - X;
			const auto y = other.Y - Y;

			return sqrt(x * x + y * y);
		}

		constexpr pointd scale(const double x, const double y) const noexcept
		{
			return {X / x, Y / y};
		}

		constexpr pointd offset(const double x, const double y) const noexcept
		{
			return {X + x, Y + y};
		}

		constexpr pointi round() const noexcept
		{
			return {iw::round(X), iw::round(Y)};
		}

		constexpr pointd operator+(const pointd point) const noexcept
		{
			return {X + point.X, Y + point.Y};
		}

		constexpr pointd operator+(const sized size) const noexcept
		{
			return {X + size.Width, Y + size.Height};
		}

		constexpr pointd operator-(const pointd point) const noexcept
		{
			return {X - point.X, Y - point.Y};
		}

		constexpr pointd operator/(const double d) const noexcept
		{
			return {X / d, Y / d};
		}

		constexpr pointd operator*(const double d) const noexcept
		{
			return {X * d, Y * d};
		}

		constexpr pointd mult(const double d) const noexcept
		{
			return {X * d, Y * d};
		}

		constexpr pointd mult(const double x, const double y) const noexcept
		{
			return {X * x, Y * y};
		}

		constexpr pointd clamp(const sized limit) const noexcept
		{
			const auto x = std::clamp(X, -limit.Width, limit.Width);
			const auto y = std::clamp(Y, -limit.Height, limit.Height);
			return {x, y};
		}

		constexpr double dot(const pointd other) const noexcept
		{
			return X * other.X + Y * other.Y;
		}

		pointd rotate(const double theta, const pointd center) const noexcept
		{
			const auto s = sin(theta);
			const auto c = cos(theta);

			const auto x = (X - center.X) * c - (center.Y - Y) * s;
			const auto y = (center.Y - Y) * c - (X - center.X) * s;

			return {center.X + x, center.Y + y};
		}

		constexpr pointd operator -() const noexcept
		{
			return {-X, -Y};
		}

		constexpr pointd operator /(const pointd other) const noexcept
		{
			return {X / other.X, Y / other.Y};
		}

		constexpr pointd& operator/=(const double d) noexcept
		{
			X /= d;
			Y /= d;
			return *this;
		}

		constexpr pointd& operator*=(const double d) noexcept
		{
			X *= d;
			Y *= d;
			return *this;
		}

		constexpr bool equals(const pointd other) const noexcept
		{
			return is_equal(X, other.X) && is_equal(Y, other.Y);
		}

		double X = 0.0;
		double Y = 0.0;
	};

	constexpr sized::sized(const pointd other) noexcept
	{
		Width = other.X;
		Height = other.Y;
	}

	class rectd
	{
	public:
		constexpr rectd() noexcept = default;

		constexpr rectd(const recti r) noexcept : X(r.x), Y(r.y), Width(r.width), Height(r.height)
		{
		}

		constexpr rectd(const double x, const double y, const double width,
		                const double height) noexcept : X(x), Y(y), Width(width), Height(height)
		{
		}

		constexpr rectd(const pointd location, const sized size) noexcept : X(location.X), Y(location.Y),
		                                                                    Width(size.Width), Height(size.Height)
		{
		}

		constexpr rectd(const rectd& other) noexcept = default;
		constexpr rectd& operator=(const rectd& other) noexcept = default;

		constexpr rectd round_d() const noexcept
		{
			return {
				static_cast<double>(iw::round(X)),
				static_cast<double>(iw::round(Y)),
				static_cast<double>(iw::round(Width)),
				static_cast<double>(iw::round(Height))
			};
		}

		constexpr recti round() const noexcept
		{
			return {iw::round(X), iw::round(Y), iw::round(Width), iw::round(Height)};
		}


		constexpr pointd location() const noexcept
		{
			return {X, Y};
		}

		constexpr sized extent() const noexcept
		{
			return {Width, Height};
		}

		constexpr void bounds(rectd* rect) const noexcept
		{
			rect->X = X;
			rect->Y = Y;
			rect->Width = Width;
			rect->Height = Height;
		}

		constexpr double left() const noexcept
		{
			return X;
		}

		constexpr double top() const noexcept
		{
			return Y;
		}

		constexpr double right() const
		{
			return X + Width;
		}

		constexpr double bottom() const
		{
			return Y + Height;
		}

		constexpr pointd top_left() const noexcept
		{
			return {left(), top()};
		}

		constexpr pointd top_right() const noexcept
		{
			return {right(), top()};
		}

		constexpr pointd bottom_right() const noexcept
		{
			return {right(), bottom()};
		}

		constexpr pointd bottom_left() const noexcept
		{
			return {left(), bottom()};
		}

		constexpr bool is_empty() const
		{
			return equiv(Width, 0.0) || equiv(Height, 0.0);
		}

		constexpr bool operator==(const rectd other) const noexcept
		{
			return equals(other);
		}

		constexpr bool operator!=(const rectd other) const noexcept
		{
			return !equals(other);
		}

		constexpr bool equals(const rectd other) const noexcept
		{
			return is_equal(X, other.X) &&
				is_equal(Y, other.Y) &&
				is_equal(Width, other.Width) &&
				is_equal(Height, other.Height);
		}

		constexpr bool contains(const double x, const double y) const noexcept
		{
			return x >= X && x < X + Width && y >= Y && y < Y + Height;
		}

		constexpr bool contains(const pointd pt) const noexcept
		{
			return contains(pt.X, pt.Y);
		}

		constexpr bool contains(const rectd rect) const noexcept
		{
			return X <= rect.X && rect.right() <= right() && Y <= rect.Y && rect.bottom() <= bottom();
		}

		constexpr rectd clamp(const rectd limit) const noexcept
		{
			double x = 0;
			double y = 0;

			if (Y < limit.Y)
				y = limit.Y - Y;

			if (X < limit.X)
				x = limit.X - X;

			if (bottom() > limit.bottom())
				y = limit.bottom() - bottom();

			if (right() > limit.right())
				x = limit.right() - right();

			return offset(x, y);
		}

		constexpr pointd center() const noexcept
		{
			return {X + Width / 2.0, Y + Height / 2.0};
		}

		constexpr rectd scale(const double dx, const double dy) const noexcept
		{
			return {X / dx, Y / dy, Width / dx, Height / dy};
		}

		constexpr rectd scale(const sized s) const noexcept
		{
			return scale(s.Width, s.Height);
		}

		constexpr rectd inflate(const double d) const noexcept
		{
			return {X - d, Y - d, Width + 2 * d, Height + 2 * d};
		}

		constexpr rectd inflate(const double dx, const double dy) const noexcept
		{
			return {X - dx, Y - dy, Width + 2 * dx, Height + 2 * dy};
		}

		constexpr rectd inflate(const pointd point) const noexcept
		{
			return inflate(point.X, point.Y);
		}

		constexpr bool intersect(const rectd rect) noexcept
		{
			return intersect(*this, *this, rect);
		}

		constexpr void set(const double x1, const double y1, const double x2, const double y2)
		{
			X = x1;
			Y = y1;
			Width = x2 - x1;
			Height = y2 - y1;
		}

		static constexpr bool intersect(rectd& c, const rectd a, const rectd b) noexcept
		{
			const auto right = std::min(a.right(), b.right());
			const auto bottom = std::min(a.bottom(), b.bottom());
			const auto left = std::min(a.left(), b.left());
			const auto top = std::min(a.top(), b.top());

			c.X = left;
			c.Y = top;
			c.Width = right - left;
			c.Height = bottom - top;
			return !c.is_empty();
		}

		constexpr bool intersects(const rectd rect) const noexcept
		{
			return left() < rect.right() &&
				top() < rect.bottom() &&
				right() > rect.left() &&
				bottom() > rect.top();
		}

		constexpr rectd offset(const pointd point) const noexcept
		{
			return offset(point.X, point.Y);
		}

		constexpr rectd offset(const double dx, const double dy) const noexcept
		{
			return {X + dx, Y + dy, Width, Height};
		}

		double X{0};
		double Y{0};
		double Width{0};
		double Height{0};
	};


	class affined
	{
	protected:
		double _trans[6]{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};

	public:
		affined() noexcept = default;
		affined(const affined& src) noexcept = default;
		affined& operator=(const affined& other) noexcept = default;
		~affined() = default;

		affined(const double d0, const double d1, const double d2, const double d3, const double d4,
		        const double d5) noexcept
		{
			_trans[0] = d0;
			_trans[1] = d1;
			_trans[2] = d2;
			_trans[3] = d3;
			_trans[4] = d4;
			_trans[5] = d5;
		}

		affined(const double aff[]) noexcept
		{
			_trans[0] = aff[0];
			_trans[1] = aff[1];
			_trans[2] = aff[2];
			_trans[3] = aff[3];
			_trans[4] = aff[4];
			_trans[5] = aff[5];
		}

		pointd transform(const pointd p) const noexcept
		{
			pointd result;
			result.X = p.X * _trans[0] + p.Y * _trans[2] + _trans[4];
			result.Y = p.X * _trans[1] + p.Y * _trans[3] + _trans[5];
			return result;
		}

		affined mult(const affined& other) const noexcept
		{
			const auto d0 = _trans[0] * other._trans[0] + _trans[1] * other._trans[2];
			const auto d1 = _trans[0] * other._trans[1] + _trans[1] * other._trans[3];
			const auto d2 = _trans[2] * other._trans[0] + _trans[3] * other._trans[2];
			const auto d3 = _trans[2] * other._trans[1] + _trans[3] * other._trans[3];
			const auto d4 = _trans[4] * other._trans[0] + _trans[5] * other._trans[2] + other._trans[4];
			const auto d5 = _trans[4] * other._trans[1] + _trans[5] * other._trans[3] + other._trans[5];

			return affined(d0, d1, d2, d3, d4, d5);
		}

		/*void Transform(float &x, float &y) const
		{
		double xx = x * _trans[0] + y * _trans[2] + _trans[4];
		double yy = x * _trans[1] + y * _trans[3] + _trans[5];
	
		x = (float)xx;
		y = (float)yy;
		}*/

		/*void TransformPoints(pointd pts[], int len) const
		{
		for(int i = 0; i < len; ++i)
		{
		pts[i] = Mult(pts[i]);
		}
		}*/

		bool equal(const affined& other) const noexcept
		{
			return is_equal(_trans[0], other._trans[0]) &&
				is_equal(_trans[1], other._trans[1]) &&
				is_equal(_trans[2], other._trans[2]) &&
				is_equal(_trans[3], other._trans[3]) &&
				is_equal(_trans[4], other._trans[4]) &&
				is_equal(_trans[5], other._trans[5]);
		}

		pointd operator*(const pointd p) const noexcept
		{
			return transform(p);
		}

		affined operator*(const affined& other) const noexcept
		{
			return mult(other);
		}

		bool operator==(const affined& other) const noexcept
		{
			return equal(other);
		}

		bool operator!=(const affined& other) const noexcept
		{
			return !equal(other);
		}

		affined invert() const noexcept
		{
			//const auto r_det = 1.0 / (_trans[0] * _trans[3] - _trans[1] * _trans[2]);

			affined result;
			//result._trans[0] = _trans[3] * r_det;
			//result._trans[1] = -_trans[1] * r_det;
			//result._trans[2] = -_trans[2] * r_det;
			//result._trans[3] = _trans[0] * r_det;
			//result._trans[4] = -_trans[4] * result._trans[0] - _trans[5] * result._trans[2];
			//result._trans[5] = -_trans[4] * result._trans[1] - _trans[5] * result._trans[3];

			const auto det = _trans[0] * _trans[3] - _trans[1] * _trans[2];
			result._trans[0] = _trans[3] / det;
			result._trans[1] = -_trans[1] / det;
			result._trans[2] = -_trans[2] / det;
			result._trans[3] = _trans[0] / det;
			result._trans[4] = (_trans[2] * _trans[5] - _trans[3] * _trans[4]) / det;
			result._trans[5] = -(_trans[0] * _trans[5] - _trans[1] * _trans[4]) / det;

			return result;
		}

		affined flip(const bool horiz, const bool vert) const noexcept
		{
			affined result;
			result._trans[0] = horiz ? -_trans[0] : _trans[0];
			result._trans[1] = horiz ? -_trans[1] : _trans[1];
			result._trans[2] = vert ? -_trans[2] : _trans[2];
			result._trans[3] = vert ? -_trans[3] : _trans[3];
			result._trans[4] = horiz ? -_trans[4] : _trans[4];
			result._trans[5] = vert ? -_trans[5] : _trans[5];
			return result;
		}

		bool is_rectilinear() const noexcept
		{
			return (equiv(_trans[1], 0.0) && equiv(_trans[2], 0.0)) ||
				(equiv(_trans[0], 0.0) && equiv(_trans[3], 0.0));
		}

		affined rotate(const double theta) const noexcept
		{
			const auto s = sin(to_radian(theta));
			const auto c = cos(to_radian(theta));

			return mult(affined(c, s, -s, c, 0, 0));
		}

		affined scale(const double s) const noexcept
		{
			return mult(affined(s, 0, 0, s, 0, 0));
		}

		affined translate(const double tx, const double ty) const noexcept
		{
			return mult(affined(1, 0, 0, 1, tx, ty));
		}

		affined translate(const pointd p) const noexcept
		{
			return translate(p.X, p.Y);
		}

		affined translate(const sized p) const noexcept
		{
			return translate(p.Width, p.Height);
		}

		affined shear(const double theta) const noexcept
		{
			const auto t = tan(to_radian(theta));
			return mult(affined(1, 0, t, 1, 0, 0));
		}

		affined shear(const double x, const double y) const noexcept
		{
			return mult(affined(1, 0, x, 1, 0, y));
		}
	};

	enum class simple_transform
	{
		none = 0,
		flip_h,
		rot_180,
		flip_v,
		transpose,
		rot_90,
		transverse,
		rot_270,
	};


	class quadd
	{
		pointd pts[4];

	public:
		quadd() noexcept = default;

		quadd(const double w, const double h) noexcept
		{
			pts[0] = pointd(0, 0);
			pts[1] = pointd(w, 0);
			pts[2] = pointd(w, h);
			pts[3] = pointd(0, h);
		}

		quadd(const double l, const double t, const double r, const double b) noexcept
		{
			pts[0] = pointd(l, t);
			pts[1] = pointd(r, t);
			pts[2] = pointd(r, b);
			pts[3] = pointd(l, b);
		}

		quadd(const recti r) noexcept
		{
			pts[0] = pointd(r.x, r.y);
			pts[1] = pointd(r.right(), r.y);
			pts[2] = pointd(r.right(), r.bottom());
			pts[3] = pointd(r.x, r.bottom());
		}

		quadd(const sizei s) noexcept
		{
			pts[0] = pointd(0, 0);
			pts[1] = pointd(s.width, 0);
			pts[2] = pointd(s.width, s.height);
			pts[3] = pointd(0, s.height);
		}

		quadd(const rectd& r) noexcept
		{
			pts[0] = pointd(r.X, r.Y);
			pts[1] = pointd(r.X + r.Width, r.Y);
			pts[2] = pointd(r.X + r.Width, r.Y + r.Height);
			pts[3] = pointd(r.X, r.Y + r.Height);
		}

		quadd(const quadd& other) noexcept = default;
		quadd& operator=(const quadd& other) noexcept = default;

		quadd& operator=(const sizei s) noexcept
		{
			pts[0] = pointd(0, 0);
			pts[1] = pointd(s.width, 0);
			pts[2] = pointd(s.width, s.height);
			pts[3] = pointd(0, s.height);
			return *this;
		}

		bool operator==(const quadd& other) const noexcept
		{
			return equals(other);
		}

		bool operator!=(const quadd& other) const noexcept
		{
			return !equals(other);
		}

		bool equals(const quadd& other) const noexcept
		{
			return pts[0] == other.pts[0] && pts[1] == other.pts[1] && pts[2] == other.pts[2] && pts[3] == other.pts[3];
		}

		bool contains(const pointi point) const noexcept
		{
			return bounding_rect_i().contains(point);
		}

		rectd bounding_rect() const noexcept
		{
			const auto l = left();
			const auto t = top();

			return {l, t, right() - l, bottom() - t};
		}

		recti bounding_rect_i() const noexcept
		{
			return {round(left()), round(top()), round(right() - left()), round(bottom() - top())};
		}

		bool is_empty() const noexcept
		{
			return equiv(left(), right()) || equiv(top(), bottom());
		}

		quadd rotate(double angle, pointd center) const noexcept;

		quadd rotate(const double angle) const noexcept
		{
			return rotate(angle, pointd(0, 0));
		};

		rectd inside_bounds(double min_size) const noexcept;

		void clear()
		{
			pts[0].X = pts[1].X = pts[2].X = pts[3].X = 0.0;
			pts[0].Y = pts[1].Y = pts[2].Y = pts[3].Y = 0.0;
		}

		quadd transform(simple_transform t) const noexcept;

		quadd transform(const affined& aff) const noexcept
		{
			quadd result;
			result.pts[0] = aff.transform(pts[0]);
			result.pts[1] = aff.transform(pts[1]);
			result.pts[2] = aff.transform(pts[2]);
			result.pts[3] = aff.transform(pts[3]);
			return result;
		}

		quadd scale(const double x, const double y) const noexcept
		{
			quadd result;
			result.pts[0] = pts[0].scale(x, y);
			result.pts[1] = pts[1].scale(x, y);
			result.pts[2] = pts[2].scale(x, y);
			result.pts[3] = pts[3].scale(x, y);
			return result;
		}

		quadd mult(const double x, const double y) const noexcept
		{
			quadd result;
			result.pts[0] = pts[0].mult(x, y);
			result.pts[1] = pts[1].mult(x, y);
			result.pts[2] = pts[2].mult(x, y);
			result.pts[3] = pts[3].mult(x, y);
			return result;
		}

		quadd scale(const sized s) const noexcept
		{
			return scale(s.Width, s.Height);
		}

		quadd scale(const double s) const noexcept
		{
			return scale(s, s);
		}

		quadd offset(const double x, const double y) const noexcept
		{
			quadd result;
			result.pts[0] = pts[0].offset(x, y);
			result.pts[1] = pts[1].offset(x, y);
			result.pts[2] = pts[2].offset(x, y);
			result.pts[3] = pts[3].offset(x, y);
			return result;
		}

		quadd offset(const pointd pt) const noexcept
		{
			return offset(pt.X, pt.Y);
		}

		quadd crop(const rectd& limit, int active_point = -1) const noexcept;
		quadd limit(const rectd& limit) const noexcept;

		static double grad(const pointd p1, const pointd p2) noexcept
		{
			const auto dx = std::abs(p2.X - p1.X);
			const auto dy = std::abs(p2.Y - p1.Y);

			return dx / dy;
		}

		pointd center_point() const noexcept
		{
			const auto x = (pts[0].X + pts[1].X + pts[2].X + pts[3].X) / 4.0;
			const auto y = (pts[0].Y + pts[1].Y + pts[2].Y + pts[3].Y) / 4.0;
			return {x, y};
		}

		static constexpr double min(const double a, const double b, const double c, const double d) noexcept
		{
			return std::min(std::min(a, b), std::min(c, d));
		}

		static constexpr double max(const double a, const double b, const double c, const double d) noexcept
		{
			return std::max(std::max(a, b), std::max(c, d));
		}

		constexpr double left() const noexcept
		{
			return min(pts[0].X, pts[1].X, pts[2].X, pts[3].X);
		}

		constexpr double top() const noexcept
		{
			return min(pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y);
		}

		constexpr double right() const noexcept
		{
			return max(pts[0].X, pts[1].X, pts[2].X, pts[3].X);
		}

		constexpr double bottom() const noexcept
		{
			return max(pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y);
		}

		constexpr double width() const noexcept
		{
			return right() - left();
		}

		constexpr double height() const noexcept
		{
			return bottom() - top();
		}

		sized actual_extent() const noexcept;

		sized extent() const noexcept
		{
			return {width(), height()};
		}

		int origin_point() const
		{
			auto cur_dist = pts[0].dist({0, 0});
			auto result = 0;

			for (int i = 1; i < 4; ++i)
			{
				const auto d = pts[i].dist({0, 0});

				if (d < cur_dist)
				{
					cur_dist = d;
					result = i;
				}
			}

			return result;
		}

		double angle(const int n = 0) const noexcept
		{
			const auto x = pts[(n + 1) % 4].X - pts[n].X;
			const auto y = pts[(n + 1) % 4].Y - pts[n].Y;
			return to_degrees(atan2(y, x));
		}

		bool has_point(const pointd pt) const noexcept
		{
			return pts[0] == pt || pts[1] == pt || pts[2] == pt || pts[3] == pt;
		}

		const pointd operator[](const int i) const noexcept
		{
			return pts[i];
		}

		pointd& operator[](const int i) noexcept
		{
			return pts[i];
		}

		affined calc_transform(const quadd& other) const noexcept;
	};

	inline quadd quadd::crop(const rectd& limit, const int active_point) const noexcept
	{
		const auto t = limit.top();
		const auto b = limit.bottom();
		const auto l = limit.left();
		const auto r = limit.right();
		const auto center = center_point();
		const auto anchor_point = (active_point + 2) % 4;
		const auto angle = this->angle();

		quadd result;
		const auto abs_angle = fabs(fmod(angle, 90.0));
		constexpr auto angle_epsilon = 0.001;
		const auto is_right_angle = abs_angle < angle_epsilon || abs_angle > 90.0 - angle_epsilon;

		if (is_right_angle)
		{
			for (auto i = 0; i < 4; ++i)
			{
				result.pts[i].X = std::clamp(pts[i].X, l, r);
				result.pts[i].Y = std::clamp(pts[i].Y, t, b);
			}
		}
		else
		{
			const auto has_anchor = active_point != -1;
			const auto anchor = has_anchor ? pts[anchor_point] : center;

			for (auto i = 0; i < 4; ++i)
			{
				if (!has_anchor || i != anchor_point)
				{
					auto x = pts[i].X;
					auto y = pts[i].Y;

					// x
					const auto dx = std::clamp(x, l, r) - x;

					auto cx = anchor.X - x;
					auto cy = anchor.Y - y;

					x = x + dx;
					if (cx != 0.0) y = y + dx * cy / cx;

					// y
					const auto dy = std::clamp(y, t, b) - y;

					cx = anchor.X - x;
					cy = anchor.Y - y;

					result.pts[i].X = (cy != 0.0) ? x + dy * cx / cy : x;
					result.pts[i].Y = y + dy;
				}
				else
				{
					result.pts[i] = pts[i];
				}
			}
		}

		result = result.rotate(-angle, center);
		result = result.inside_bounds(1.0);
		result = result.rotate(angle, center);
		result = result.limit(limit);

		return result;
	}

	inline sized quadd::actual_extent() const noexcept
	{
		const auto angle = -this->angle();
		const auto theta = to_radian(angle);
		const auto s = sin(theta);
		const auto c = cos(theta);

		const auto center = pts[0];
		const auto px = pts[1];
		const auto py = pts[3];

		const auto x = (px.X - center.X) * c - (px.Y - center.Y) * s;
		const auto y = (py.X - center.X) * s + (py.Y - center.Y) * c;

		return {fabs(x), fabs(y)};
	}

	inline quadd quadd::limit(const rectd& limit) const noexcept
	{
		auto cx = 0.0;
		auto cy = 0.0;

		const auto l = left();
		const auto t = top();
		const auto r = right();
		const auto b = bottom();

		const auto ll = limit.left();
		const auto lt = limit.top();
		const auto lr = limit.right();
		const auto lb = limit.bottom();

		if (l < ll && r < lr) cx = std::min(ll - l, lr - r);
		if (t < lt && b < lb) cy = std::min(lt - t, lb - b);
		if (r > lr && l > ll) cx = std::max(lr - r, ll - l);
		if (b > lb && t > lt) cy = std::max(lb - b, lt - t);

		return offset(cx, cy);
	}

	inline quadd quadd::transform(const simple_transform t) const noexcept
	{
		quadd result;

		switch (t)
		{
		default:
		case simple_transform::none:
			result = *this;
			break;

		case simple_transform::flip_h:
			result.pts[0] = pts[1];
			result.pts[1] = pts[0];
			result.pts[2] = pts[3];
			result.pts[3] = pts[2];
			break;

		case simple_transform::rot_180:
			result.pts[0] = pts[2];
			result.pts[1] = pts[3];
			result.pts[2] = pts[0];
			result.pts[3] = pts[1];
			break;

		case simple_transform::flip_v:
			result.pts[0] = pts[3];
			result.pts[1] = pts[2];
			result.pts[2] = pts[1];
			result.pts[3] = pts[0];
			break;

		case simple_transform::transpose:
			result.pts[0] = pts[0];
			result.pts[1] = pts[3];
			result.pts[2] = pts[2];
			result.pts[3] = pts[1];
			break;

		case simple_transform::rot_90:
			result.pts[1] = pts[0];
			result.pts[2] = pts[1];
			result.pts[3] = pts[2];
			result.pts[0] = pts[3];
			break;

		case simple_transform::transverse:
			result.pts[0] = pts[2];
			result.pts[1] = pts[1];
			result.pts[2] = pts[0];
			result.pts[3] = pts[3];
			break;

		case simple_transform::rot_270:
			result.pts[3] = pts[0];
			result.pts[0] = pts[1];
			result.pts[1] = pts[2];
			result.pts[2] = pts[3];
			break;
		}

		return result;
	}


	inline quadd quadd::rotate(const double angle, const pointd center) const noexcept
	{
		quadd result;

		const auto theta = to_radian(angle);
		const auto s = sin(theta);
		const auto c = cos(theta);

		for (int i = 0; i < 4; ++i)
		{
			const auto point = pts[i];
			const auto x = (point.X - center.X) * c - (point.Y - center.Y) * s;
			const auto y = (point.X - center.X) * s + (point.Y - center.Y) * c;
			result.pts[i] = pointd(center.X + x, center.Y + y);
		}

		return result;
	}

	inline rectd quadd::inside_bounds(const double min_size) const noexcept
	{
		double xx[] = {pts[0].X, pts[1].X, pts[2].X, pts[3].X};
		double yy[] = {pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y};

		std::sort(xx, xx + 4);
		std::sort(yy, yy + 4);

		const auto cx = (xx[1] + xx[2]) / 2.0;
		const auto cy = (yy[1] + yy[2]) / 2.0;
		const auto cxy = min_size / 2.0;

		return {
			std::min(xx[1], cx - cxy),
			std::min(yy[1], cy - cxy),
			std::max(xx[2] - xx[1], min_size),
			std::max(yy[2] - yy[1], min_size)
		};
	}

	inline affined quadd::calc_transform(const quadd& other) const noexcept
	{
		const auto s = other.pts[0].dist(other.pts[1]) / pts[0].dist(pts[1]);
		return affined().translate(-pts[0]).rotate(other.angle() - angle()).scale(s).translate(other.pts[0]);
	}
} // namespace iw

#pragma pop_macro("max")
#pragma pop_macro("min")
