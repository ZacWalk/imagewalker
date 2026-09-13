// ImageWalker by Zac Walker
// Models renderer-independent image scale, source-space center, anchoring, and stepped zoom.

#pragma once

#include "util_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace iw::ui
{
	class ZoomModel
	{
	public:
		enum class ScaleMode { fit, explicitScale };
		struct SourcePoint { double x{0.5}; double y{0.5}; };

		ScaleMode mode() const { return mode_; }
		bool is_fit() const { return mode_ == ScaleMode::fit; }
		double explicit_scale() const { return explicitScale_; }
		double center_x() const { return centerX_; }
		double center_y() const { return centerY_; }

		void set_source(const sizei source) { source_ = source; }
		void carry_to_source(const sizei source, const recti viewport)
		{
			source_ = source;
			fitForCarriedScale_ = !is_fit() && explicitScale_ < fit_scale(viewport);
		}
		sizei source() const { return source_; }

		double fit_scale(const recti viewport, const bool enlarge = false) const
		{
			if (source_.width <= 0 || source_.height <= 0 || viewport.width <= 0 || viewport.height <= 0)
				return 1.0;
			const double scale = (std::min)(static_cast<double>(viewport.width) / source_.width,
			                                static_cast<double>(viewport.height) / source_.height);
			return enlarge ? scale : (std::min)(1.0, scale);
		}

		double effective_scale(const recti viewport) const
		{
			const double fitScale = fit_scale(viewport);
			return is_fit() || (fitForCarriedScale_ && explicitScale_ < fitScale) ? fitScale : explicitScale_;
		}

		bool is_effectively_fit(const recti viewport) const
		{
			return is_fit() || effective_scale(viewport) == fit_scale(viewport);
		}

		static double minimum_scale(const double fitScale) { return (std::min)(0.05, fitScale); }
		static double maximum_scale(const double fitScale) { return (std::max)(16.0, fitScale); }
		static int accelerated_pan_offset(const int pointerOffset, const int ramp)
		{
			if (ramp <= 0) return pointerOffset;
			const double magnitude = std::abs(static_cast<double>(pointerOffset));
			const double accelerated = magnitude + magnitude * magnitude / ramp;
			const double signedOffset = std::copysign(accelerated, static_cast<double>(pointerOffset));
			return static_cast<int>(std::clamp(
				std::lround(signedOffset),
				static_cast<long>((std::numeric_limits<int>::min)()),
				static_cast<long>((std::numeric_limits<int>::max)())));
		}

		recti destination(const recti viewport) const
		{
			return destination(viewport, effective_scale(viewport), centerX_, centerY_);
		}

		recti destination_at_scale(const recti viewport, const double scale) const
		{
			return destination(viewport, scale, centerX_, centerY_);
		}

		SourcePoint source_at(const recti viewport, const pointi point) const
		{
			const recti current = destination(viewport);
			return {
				std::clamp((point.x - current.x) / static_cast<double>((std::max)(1, current.width)), 0.0, 1.0),
				std::clamp((point.y - current.y) / static_cast<double>((std::max)(1, current.height)), 0.0, 1.0)
			};
		}

		void set_fit()
		{
			mode_ = ScaleMode::fit;
			fitForCarriedScale_ = false;
			centerX_ = centerY_ = 0.5;
		}

		void set_center(const double x, const double y)
		{
			centerX_ = std::clamp(x, 0.0, 1.0);
			centerY_ = std::clamp(y, 0.0, 1.0);
		}

		void set_explicit(const double scale, const recti viewport, const pointi anchor)
		{
			set_explicit_at_source(scale, viewport, anchor, source_at(viewport, anchor));
		}

		void set_explicit_at_source(const double scale, const recti viewport, const pointi anchor,
		                            const SourcePoint sourcePoint)
		{
			const double fitScale = fit_scale(viewport);
			explicitScale_ = std::clamp(scale, minimum_scale(fitScale), maximum_scale(fitScale));
			mode_ = ScaleMode::explicitScale;
			fitForCarriedScale_ = false;

			const int width = scaled_extent(source_.width, explicitScale_);
			const int height = scaled_extent(source_.height, explicitScale_);
			const double viewportCenterX = viewport.x + viewport.width / 2.0;
			const double viewportCenterY = viewport.y + viewport.height / 2.0;
			set_center((viewportCenterX - anchor.x + sourcePoint.x * width) / (std::max)(1, width),
			           (viewportCenterY - anchor.y + sourcePoint.y * height) / (std::max)(1, height));
		}

		void pan_by(const recti viewport, const int x, const int y)
		{
			if (is_fit()) return;
			const recti current = destination(viewport);
			const int width = scaled_extent(source_.width, explicitScale_);
			const int height = scaled_extent(source_.height, explicitScale_);
			const double viewportCenterX = viewport.x + viewport.width / 2.0;
			const double viewportCenterY = viewport.y + viewport.height / 2.0;
			if (width > viewport.width)
				centerX_ = std::clamp((viewportCenterX - (current.x + x)) / width, 0.0, 1.0);
			else centerX_ = 0.5;
			if (height > viewport.height)
				centerY_ = std::clamp((viewportCenterY - (current.y + y)) / height, 0.0, 1.0);
			else centerY_ = 0.5;
		}

		void step(const int direction, const recti viewport, const pointi anchor)
		{
			if (!direction) return;
			const double fitScale = fit_scale(viewport);
			if (direction < 0 && is_effectively_fit(viewport))
			{
				set_fit();
				return;
			}
			const double current = effective_scale(viewport);
			auto stops = ladder(fitScale);
			double target = current;
			if (direction > 0)
			{
				const auto next = std::ranges::find_if(stops, [current](const double value)
				{
					return value > current + 1e-9;
				});
				if (next != stops.end()) target = *next;
			}
			else
			{
				for (auto next = stops.rbegin(); next != stops.rend(); ++next)
					if (*next < current - 1e-9)
					{
						target = *next;
						break;
					}
			}
			if (direction < 0 && current > fitScale && target <= fitScale + 1e-9)
			{
				set_fit();
				return;
			}
			set_explicit(target, viewport, anchor);
		}

	private:
		static int scaled_extent(const int source, const double scale)
		{
			return (std::max)(1, static_cast<int>(std::lround(source * scale)));
		}

		recti destination(const recti viewport, const double scale, const double centerX,
		                  const double centerY) const
		{
			const int width = scaled_extent(source_.width, scale);
			const int height = scaled_extent(source_.height, scale);
			int x = static_cast<int>(std::lround(viewport.x + viewport.width / 2.0 - centerX * width));
			int y = static_cast<int>(std::lround(viewport.y + viewport.height / 2.0 - centerY * height));
			if (width <= viewport.width) x = viewport.x + (viewport.width - width) / 2;
			else x = std::clamp(x, viewport.right() - width, viewport.x);
			if (height <= viewport.height) y = viewport.y + (viewport.height - height) / 2;
			else y = std::clamp(y, viewport.bottom() - height, viewport.y);
			return {x, y, width, height};
		}

		static std::vector<double> ladder(const double fitScale)
		{
			constexpr std::array fixed{
				0.05, 0.07, 0.10, 0.15, 0.20, 0.25, 0.33, 0.50, 0.67, 0.75,
				1.00, 1.50, 2.00, 3.00, 4.00, 6.00, 8.00, 12.00, 16.00
			};
			std::vector<double> result(fixed.begin(), fixed.end());
			result.push_back(fitScale);
			std::ranges::sort(result);
			result.erase(std::unique(result.begin(), result.end(), [](const double left, const double right)
			{
				return std::abs(left - right) < 1e-9;
			}), result.end());
			return result;
		}

		sizei source_;
		ScaleMode mode_{ScaleMode::fit};
		double explicitScale_{1.0};
		double centerX_{0.5};
		double centerY_{0.5};
		bool fitForCarriedScale_{};
	};
}