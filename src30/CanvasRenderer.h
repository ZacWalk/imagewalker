// ImageWalker by Zac Walker
// Declares the rendering facade that isolates canvas code from Win32 and GDI drawing details.

#pragma once

#include "Platform.h"
#include <iw/imaging/Color.h>
#include "AppLayout.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace iw::ui
{
	struct ArgbImage
	{
		int width{};
		int height{};
		std::span<const std::uint32_t> pixels;
		bool alpha{};
		std::uint64_t cacheToken{};
	};

	class CanvasRenderer
	{
	public:
		CanvasRenderer(platform::DrawContext& draw, platform::FontPtr font = {})
			: draw_(draw), font_(std::move(font))
		{
		}

		CanvasRenderer(const CanvasRenderer&) = delete;
		CanvasRenderer& operator=(const CanvasRenderer&) = delete;

		void fill(app::recti rect, color value) const;
		void blend_fill(app::recti rect, color value) const;
		void text(std::wstring_view value, app::recti rect, color textColor, std::uint32_t format) const;
		app::sizei measure_text(std::wstring_view value,
		                   std::uint32_t format = platform::TextFormat::singleLine) const;
		void image(const ArgbImage& image, app::recti destination) const;
		void focus(app::recti rect) const;
		void outline(app::recti rect, color value, int width = 1) const;
		void scrollbar(const ScrollBar& scroll) const;
		void clip(app::recti rect) const;
		void reset_clip() const;

	private:
		platform::DrawContext& draw_;
		platform::FontPtr font_;
	};
}
