// ImageWalker by Zac Walker
// Provides retained flex layout, details columns, and scrollbar primitives shared by all five applications.

#pragma once

#include "util_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

// See util_geometry.h: windows.h has min and max as macros in the WTL trees.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

namespace iw::ui
{
	inline constexpr int propertyRowHeight = 26;

	enum class Axis { row, column };

	struct FlexStyle
	{
		Axis axis{Axis::row};
		bool wrap{};
		int basis{};
		int grow{};
		int gap{};
		int padding{};
		int minimum{};
	};

	struct Element
	{
		int id{-1};
		FlexStyle style;
		sizei intrinsic;
		recti bounds;
		std::vector<Element> children;
	};

	class FlexBox
	{
	public:
		static sizei arrange(Element& element, const recti bounds)
		{
			element.bounds = bounds;
			if (element.children.empty()) return {bounds.width, bounds.height};
			return element.style.wrap ? arrange_wrapped(element) : arrange_linear(element);
		}

		static const Element* hit_test(const Element& element, const pointi point)
		{
			if (!element.bounds.contains(point)) return nullptr;
			for (auto child = element.children.rbegin(); child != element.children.rend(); ++child)
				if (const auto* hit = hit_test(*child, point)) return hit;
			return &element;
		}

		static sizei arrange_list(Element& element, const recti bounds, const int rowHeight)
		{
			element.style.axis = Axis::column;
			for (auto& child : element.children) child.intrinsic.height = rowHeight;
			arrange(element, bounds);
			const int contentHeight = element.style.padding * 2 +
				static_cast<int>(element.children.size()) * rowHeight +
				(std::max)(0, static_cast<int>(element.children.size()) - 1) * element.style.gap;
			return {bounds.width, contentHeight};
		}

	private:
		static sizei arrange_linear(Element& element)
		{
			const bool row = element.style.axis == Axis::row;
			const int mainExtent = row ? element.bounds.width : element.bounds.height;
			const int crossExtent = row ? element.bounds.height : element.bounds.width;
			const int innerMain = (std::max)(0, mainExtent - element.style.padding * 2);
			const int innerCross = (std::max)(0, crossExtent - element.style.padding * 2);
			int fixed = element.style.gap * (static_cast<int>(element.children.size()) - 1);
			int grow = 0;
			for (const auto& child : element.children)
			{
				fixed += child.style.basis ? child.style.basis : (row ? child.intrinsic.width : child.intrinsic.height);
				grow += child.style.grow;
			}
			const int available = (std::max)(0, innerMain - fixed);
			int cursor = (row ? element.bounds.x : element.bounds.y) + element.style.padding;
			for (auto& child : element.children)
			{
				const int natural = child.style.basis
					                    ? child.style.basis
					                    : (row ? child.intrinsic.width : child.intrinsic.height);
				const int extent = (std::max)(child.style.minimum,
				                              natural + (grow ? available * child.style.grow / grow : 0));
				const recti childBounds = row
					                          ? recti{
						                          cursor, element.bounds.y + element.style.padding, extent, innerCross
					                          }
					                          : recti{
						                          element.bounds.x + element.style.padding, cursor, innerCross, extent
					                          };
				arrange(child, childBounds);
				cursor += extent + element.style.gap;
			}
			return {element.bounds.width, element.bounds.height};
		}

		static sizei arrange_wrapped(Element& element)
		{
			const int left = element.bounds.x + element.style.padding;
			const int right = element.bounds.right() - element.style.padding;
			int x = left;
			int y = element.bounds.y + element.style.padding;
			int lineHeight = 0;
			for (auto& child : element.children)
			{
				const int width = (std::max)(child.style.minimum,
				                             child.style.basis ? child.style.basis : child.intrinsic.width);
				const int height = child.intrinsic.height;
				if (x != left && x + width > right)
				{
					x = left;
					y += lineHeight + element.style.gap;
					lineHeight = 0;
				}
				arrange(child, {x, y, width, height});
				x += width + element.style.gap;
				lineHeight = (std::max)(lineHeight, height);
			}
			return {element.bounds.width, y + lineHeight + element.style.padding - element.bounds.y};
		}
	};

	struct ScrollBar
	{
		Axis axis{Axis::column};
		recti track;
		recti thumb;
		int viewportExtent{};
		int contentExtent{};
		int offset{};

		bool vertical() const { return axis == Axis::column; }
		bool visible() const { return contentExtent > viewportExtent && viewportExtent > 0; }
		int maximum() const { return (std::max)(0, contentExtent - viewportExtent); }

		void layout(const recti panel, const int content, const int requestedOffset, const int thickness = 12,
		            const int minimumThumb = 28)
		{
			viewportExtent = vertical() ? panel.height : panel.width;
			contentExtent = (std::max)(0, content);
			offset = std::clamp(requestedOffset, 0, maximum());
			if (!visible())
			{
				track = thumb = {};
				return;
			}
			track = vertical()
				        ? recti{panel.right() - thickness, panel.y, thickness, panel.height}
				        : recti{panel.x, panel.bottom() - thickness, panel.width, thickness};
			const int trackExtent = vertical() ? track.height : track.width;
			const auto proportional = static_cast<std::int64_t>(trackExtent) * viewportExtent / contentExtent;
			const int thumbExtent = (std::min)(trackExtent,
			                                   (std::max)(minimumThumb, static_cast<int>(proportional)));
			const std::int64_t travel = trackExtent - thumbExtent;
			const int position = maximum() ? static_cast<int>(travel * offset / maximum()) : 0;
			thumb = vertical()
				        ? recti{track.x, track.y + position, thickness, thumbExtent}
				        : recti{track.x + position, track.y, thumbExtent, thickness};
		}

		recti viewport(const recti panel) const
		{
			if (!visible()) return panel;
			return vertical()
				       ? recti{panel.x, panel.y, panel.width - track.width, panel.height}
				       : recti{panel.x, panel.y, panel.width, panel.height - track.height};
		}

		bool scroll_by(const int delta)
		{
			const int previous = offset;
			offset = std::clamp(offset + delta, 0, maximum());
			return offset != previous;
		}

		void set_thumb_position(const int position)
		{
			const std::int64_t travel = vertical() ? track.height - thumb.height : track.width - thumb.width;
			const int origin = vertical() ? track.y : track.x;
			const std::int64_t moved = std::clamp<std::int64_t>(position - origin, 0, travel);
			offset = travel > 0 ? static_cast<int>(moved * maximum() / travel) : 0;
		}
	};

	struct VerticalScroll : ScrollBar
	{
		VerticalScroll() { axis = Axis::column; }
	};

	struct HorizontalScroll : ScrollBar
	{
		HorizontalScroll() { axis = Axis::row; }
	};

	inline recti normalize(const pointi first, const pointi second)
	{
		return {
			(std::min)(first.x, second.x), (std::min)(first.y, second.y),
			std::abs(second.x - first.x), std::abs(second.y - first.y)
		};
	}

	inline recti fit_centered(const sizei source, const recti bounds)
	{
		if (source.width <= 0 || source.height <= 0) return {bounds.x, bounds.y, 0, 0};
		const double scale = (std::min)(static_cast<double>(bounds.width) / source.width,
		                                static_cast<double>(bounds.height) / source.height);
		const int width = (std::max)(1, static_cast<int>(std::lround(source.width * scale)));
		const int height = (std::max)(1, static_cast<int>(std::lround(source.height * scale)));
		return {bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2, width, height};
	}

	// Packs cells into bounds by recursively splitting on the heavier half, giving the image whose
	// aspect best matches the canvas the largest share. Ported from Diffractor's ui::layout_collage.
	inline std::vector<recti> layout_collage(const recti bounds, const std::vector<sizei>& dimensions)
	{
		constexpr size_t maximumCells = 24;
		constexpr double goldenRatio = 1.6180339887498948482;
		const size_t count = (std::min)(dimensions.size(), maximumCells);
		if (count == 0 || bounds.is_empty()) return {};
		std::vector<recti> results(count);
		if (count == 1)
		{
			results.front() = bounds;
			return results;
		}

		std::vector<double> aspects;
		aspects.reserve(count);
		for (size_t index = 0; index < count; ++index)
		{
			const sizei extent = dimensions[index];
			aspects.push_back(extent.width > 0 && extent.height > 0
				                  ? static_cast<double>(extent.width) / extent.height
				                  : 1.0);
		}

		const double canvasAspect = static_cast<double>(bounds.width) / bounds.height;
		const auto feature = static_cast<size_t>(std::min_element(aspects.begin(), aspects.end(),
		                                                          [canvasAspect](const double left, const double right)
		                                                          {
			                                                          return std::abs(std::log(left / canvasAspect)) <
				                                                          std::abs(std::log(right / canvasAspect));
		                                                          }) - aspects.begin());

		std::vector<double> weights(count, 1.0);
		weights[feature] = goldenRatio * goldenRatio;
		if (count >= 6) weights[(feature + count / 2) % count] = goldenRatio;

		const auto weight_sum = [&weights](const size_t start, const size_t end)
		{
			return std::accumulate(weights.begin() + start, weights.begin() + end, 0.0);
		};

		const auto place = [&](const auto& self, const size_t start, const size_t end, const recti area) -> void
		{
			if (end - start == 1)
			{
				results[start] = area;
				return;
			}
			const double total = weight_sum(start, end);
			size_t divide = start + 1;
			double balance = (std::numeric_limits<double>::max)();
			for (size_t candidate = start + 1; candidate < end; ++candidate)
			{
				const double offset = std::abs(weight_sum(start, candidate) / total - 0.5);
				if (offset < balance)
				{
					balance = offset;
					divide = candidate;
				}
			}
			const double fraction = weight_sum(start, divide) / total;
			if (area.width > 1 && (area.width >= area.height || area.height <= 1))
			{
				const int x = std::clamp(area.x + static_cast<int>(std::lround(area.width * fraction)),
				                         area.x + 1, area.right() - 1);
				self(self, start, divide, {area.x, area.y, x - area.x, area.height});
				self(self, divide, end, {x, area.y, area.right() - x, area.height});
			}
			else if (area.height > 1)
			{
				const int y = std::clamp(area.y + static_cast<int>(std::lround(area.height * fraction)),
				                         area.y + 1, area.bottom() - 1);
				self(self, start, divide, {area.x, area.y, area.width, y - area.y});
				self(self, divide, end, {area.x, y, area.width, area.bottom() - y});
			}
			else
			{
				for (size_t index = start; index < end; ++index) results[index] = area;
			}
		};

		place(place, 0, count, bounds);
		return results;
	}

	inline std::array<recti, 5> detail_columns(const recti basis)
	{
		constexpr std::array weights{38, 24, 14, 12, 12};
		std::array<recti, weights.size()> result{};
		int x = basis.x;
		int remaining = basis.width;
		int remainingWeight = 100;
		for (size_t index = 0; index < result.size(); ++index)
		{
			const int width = index + 1 == result.size() ? remaining : remaining * weights[index] / remainingWeight;
			result[index] = {x, basis.y, width, basis.height};
			x += width;
			remaining -= width;
			remainingWeight -= weights[index];
		}
		return result;
	}

	inline std::pair<size_t, size_t> visible_range(const std::vector<Element>& children, const recti viewport)
	{
		const auto first = std::ranges::lower_bound(children, viewport.y, {},
		                                            [](const Element& child) { return child.bounds.bottom(); });
		const auto last = std::ranges::upper_bound(children, viewport.bottom(), {},
		                                           [](const Element& child) { return child.bounds.y; });
		return {
			static_cast<size_t>(first - children.begin()),
			static_cast<size_t>((std::max)(first, last) - children.begin())
		};
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
