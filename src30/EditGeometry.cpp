#include "EditGeometry.h"
#include <algorithm>
#include <cmath>
namespace iw::edits
{
	CropHandle crop_handle(const recti crop, const pointi point, const int radius)
	{
		if (crop.width <= 0 || crop.height <= 0 || radius < 0) return CropHandle::none;
		const bool l = std::abs(static_cast<long long>(point.x) - crop.x) <= radius;
		const bool r = std::abs(static_cast<long long>(point.x) - crop.right()) <= radius;
		const bool t = std::abs(static_cast<long long>(point.y) - crop.y) <= radius;
		const bool b = std::abs(static_cast<long long>(point.y) - crop.bottom()) <= radius;
		if (l && t) return CropHandle::topLeft;
		if (r && t) return CropHandle::topRight;
		if (r && b) return CropHandle::bottomRight;
		if (l && b) return CropHandle::bottomLeft;
		if (point.y >= crop.y && point.y <= crop.bottom())
		{ if (l) return CropHandle::left; if (r) return CropHandle::right; }
		if (point.x >= crop.x && point.x <= crop.right())
		{ if (t) return CropHandle::top; if (b) return CropHandle::bottom; }
		return crop.contains(point) ? CropHandle::move : CropHandle::none;
	}

	recti drag_crop(const recti initial, const recti bounds, const CropHandle handle, const pointi delta)
	{
		if (bounds.width <= 0 || bounds.height <= 0 || initial.width <= 0 || initial.height <= 0) return {};
		const int width = (std::min)(initial.width, bounds.width), height = (std::min)(initial.height, bounds.height);
		int l = std::clamp(initial.x, bounds.x, bounds.right() - width), r = l + width;
		int t = std::clamp(initial.y, bounds.y, bounds.bottom() - height), b = t + height;
		const auto shift = [](const int value, const int deltaValue, const int low, const int high)
			{ return static_cast<int>(std::clamp(static_cast<long long>(value) + deltaValue,
				static_cast<long long>(low), static_cast<long long>(high))); };
		if (handle == CropHandle::move)
			return {shift(l, delta.x, bounds.x, bounds.right() - width),
				shift(t, delta.y, bounds.y, bounds.bottom() - height), width, height};
		if (handle == CropHandle::left || handle == CropHandle::topLeft || handle == CropHandle::bottomLeft)
			l = shift(l, delta.x, bounds.x, r - 1);
		if (handle == CropHandle::right || handle == CropHandle::topRight || handle == CropHandle::bottomRight)
			r = shift(r, delta.x, l + 1, bounds.right());
		if (handle == CropHandle::top || handle == CropHandle::topLeft || handle == CropHandle::topRight)
			t = shift(t, delta.y, bounds.y, b - 1);
		if (handle == CropHandle::bottom || handle == CropHandle::bottomLeft || handle == CropHandle::bottomRight)
			b = shift(b, delta.y, t + 1, bounds.bottom());
		return {l, t, r - l, b - t};
	}
}
