// ImageWalker by Zac Walker
// Models monotonic displayed-image quality, outstanding resolution need, and failure state.

#pragma once

#include <algorithm>
#include <cstdint>

namespace iw::ui
{
	class LoadingModel
	{
	public:
		enum class Phase : std::uint8_t { none, placeholder, thumbnail, source };
		enum class Failure : std::uint8_t { none, unreadable, unsupported, offline };

		void begin(const std::uint64_t generation, const int requiredWidth, const int requiredHeight,
		           const bool shapedPlaceholder)
		{
			generation_ = generation;
			phase_ = shapedPlaceholder ? Phase::placeholder : Phase::none;
			failure_ = Failure::none;
			heldWidth_ = heldHeight_ = 0;
			requiredWidth_ = requiredHeight_ = 0;
			require(requiredWidth, requiredHeight);
		}

		void seed_thumbnail(const int width, const int height)
		{
			if (width <= 0 || height <= 0) return;
			phase_ = Phase::thumbnail;
			heldWidth_ = width;
			heldHeight_ = height;
			clear_satisfied_need();
		}

		void require(const int width, const int height)
		{
			requiredWidth_ = (std::max)(requiredWidth_, (std::max)(0, width));
			requiredHeight_ = (std::max)(requiredHeight_, (std::max)(0, height));
		}

		void cap_need_to_source(const int width, const int height)
		{
			if (width > 0) requiredWidth_ = (std::min)(requiredWidth_, width);
			if (height > 0) requiredHeight_ = (std::min)(requiredHeight_, height);
			clear_satisfied_need();
		}

		bool apply(const std::uint64_t generation, const Phase phase, const int width, const int height)
		{
			if (generation != generation_ || width <= 0 || height <= 0 || phase < phase_) return false;
			if (phase == phase_ &&
				(width < heldWidth_ || height < heldHeight_ || (width == heldWidth_ && height == heldHeight_)))
				return false;
			phase_ = phase;
			heldWidth_ = width;
			heldHeight_ = height;
			failure_ = Failure::none;
			clear_satisfied_need();
			return true;
		}

		bool fail(const std::uint64_t generation, const Failure failure)
		{
			if (generation != generation_) return false;
			failure_ = failure;
			return true;
		}

		std::uint64_t generation() const { return generation_; }
		Phase phase() const { return phase_; }
		Failure failure() const { return failure_; }
		bool needs_more() const { return requiredWidth_ > 0 || requiredHeight_ > 0; }
		bool provisional() const { return failure_ == Failure::none && needs_more(); }
		int held_width() const { return heldWidth_; }
		int held_height() const { return heldHeight_; }

	private:
		void clear_satisfied_need()
		{
			if (heldWidth_ >= requiredWidth_ && heldHeight_ >= requiredHeight_)
				requiredWidth_ = requiredHeight_ = 0;
		}

		std::uint64_t generation_{};
		Phase phase_{Phase::none};
		Failure failure_{Failure::none};
		int heldWidth_{};
		int heldHeight_{};
		int requiredWidth_{};
		int requiredHeight_{};
	};
}