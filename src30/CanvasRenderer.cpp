// ImageWalker by Zac Walker
// Implements the canvas rendering facade over the platform-independent drawing context.

#include "Platform.h"
#include "CanvasRenderer.h"
#include "ImageResampler.h"

namespace iw::ui
{
	void CanvasRenderer::fill(const recti rect, const color value) const
	{
		draw_.fill(rect, value);
	}

	void CanvasRenderer::blend_fill(const recti rect, const color value) const
	{
		draw_.blend_fill(rect, value);
	}

	void CanvasRenderer::text(const std::wstring_view value, const recti rect, const color textColor,
	                          const std::uint32_t format) const
	{
		draw_.text(value, rect, textColor, format, font_);
	}

	sizei CanvasRenderer::measure_text(const std::wstring_view value, const std::uint32_t format) const
	{
		return draw_.measure_text(value, format, font_);
	}

	void CanvasRenderer::image(const ArgbImage& imageValue, const recti destination) const
	{
		static thread_local ResampleCache cache;
		const recti visible = destination.intersection(draw_.clip_rect());
		if (visible.is_empty() || destination.width <= 0 || destination.height <= 0) return;
		const double scaleX = static_cast<double>(imageValue.width) / destination.width;
		const double scaleY = static_cast<double>(imageValue.height) / destination.height;
		const SampleRect source{
			(visible.x - destination.x) * scaleX, (visible.y - destination.y) * scaleY,
			visible.width * scaleX, visible.height * scaleY
		};
		const auto pixels = cache.resample(imageValue.pixels, {imageValue.width, imageValue.height}, source,
		                                   visible.extent(), imageValue.cacheToken);
		if (!pixels.empty()) draw_.image(pixels, visible.extent(), visible, imageValue.alpha);
	}

	void CanvasRenderer::focus(const recti rect) const
	{
		draw_.focus(rect);
	}

	void CanvasRenderer::outline(const recti rect, const color value, const int width) const
	{
		draw_.outline(rect, value, width);
	}

	void CanvasRenderer::scrollbar(const ScrollBar& scroll) const
	{
		if (!scroll.visible()) return;
		fill(scroll.track, platform::system_color(platform::SystemColor::scrollbar));
		const int maximumInset = (std::max)(0, (std::min)(scroll.thumb.width, scroll.thumb.height) - 1) / 2;
		const int thickness = scroll.vertical() ? scroll.track.width : scroll.track.height;
		const int inset = std::clamp(thickness / 6, 0, maximumInset);
		fill({
			     scroll.thumb.x + inset, scroll.thumb.y + inset,
			     scroll.thumb.width - inset * 2, scroll.thumb.height - inset * 2
		     },
		     platform::system_color(platform::SystemColor::shadow));
	}

	void CanvasRenderer::clip(const recti rect) const
	{
		draw_.clip(rect);
	}

	void CanvasRenderer::reset_clip() const { draw_.reset_clip(); }
}
