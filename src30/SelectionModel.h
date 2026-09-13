// ImageWalker by Zac Walker
// Implements Explorer-style focus, range, toggle, keyboard, and marquee selection semantics.

#pragma once

#include "util_layout.h"
#include <algorithm>
#include <functional>
#include <vector>

namespace iw::ui
{
	class SelectionModel
	{
	public:
		void reset(const int count)
		{
			count_ = (std::max)(count, 0);
			selected_.assign(static_cast<size_t>(count_), false);
			selectedCount_ = 0;
			marqueeBase_.clear();
			focus_ = count_ ? 0 : -1;
			anchor_ = focus_;
		}

		void click(const int index, const bool control, const bool shift)
		{
			if (!valid(index)) return;
			if (shift && anchor_ >= 0) extend_range(index, control);
			else if (control)
			{
				set_selected(index, !contains(index));
				anchor_ = index;
			}
			else
			{
				select_only(index);
				anchor_ = index;
			}
			focus_ = index;
		}

		// Explorer keeps focus on the item the marquee started over, not on the last item it sweeps.
		void begin_marquee(const bool additive, const int originIndex = -1)
		{
			if (additive) marqueeBase_ = selected_;
			else marqueeBase_.assign(selected_.size(), false);
			if (valid(originIndex)) focus_ = anchor_ = originIndex;
		}

		void marquee(const std::vector<int>& hitIds)
		{
			selected_ = marqueeBase_;
			selected_.resize(static_cast<size_t>(count_), false);
			selectedCount_ = static_cast<size_t>(std::ranges::count(selected_, true));
			for (const int id : hitIds) set_selected(id, true);
		}

		void marquee(const std::vector<Element>& items, const recti area)
		{
			std::vector<int> hits;
			hits.reserve(items.size());
			for (const auto& item : items) if (item.bounds.intersects(area)) hits.push_back(item.id);
			marquee(hits);
		}

		void navigate(const int index, const bool control, const bool shift)
		{
			if (!valid(index)) return;
			if (shift && anchor_ >= 0) extend_range(index, control);
			else if (!control)
			{
				select_only(index);
				anchor_ = index;
			}
			// Explorer carries the anchor along with a Ctrl-navigated focus.
			else anchor_ = index;
			focus_ = index;
		}

		void clear()
		{
			clear_selection();
			anchor_ = focus_;
		}

		void select_all()
		{
			selected_.assign(static_cast<size_t>(count_), true);
			selectedCount_ = static_cast<size_t>(count_);
			if (count_) focus_ = anchor_ = 0;
		}

		void restore(const int count, const std::vector<int>& selected, const int focus,
		             const int fallbackFocus = -1)
		{
			reset(count);
			for (const int index : selected) set_selected(index, true);
			if (valid(focus)) focus_ = anchor_ = focus;
			else if (count_ && fallbackFocus >= 0) focus_ = anchor_ = std::clamp(fallbackFocus, 0, count_ - 1);
		}

		int move_focus(const int delta)
		{
			if (!count_) return -1;
			focus_ = focus_ < 0 ? (delta >= 0 ? 0 : count_ - 1) : std::clamp(focus_ + delta, 0, count_ - 1);
			select_only(focus_);
			anchor_ = focus_;
			return focus_;
		}

		void compare_select(const int index, const bool secondary)
		{
			if (!valid(index)) return;
			if (!secondary || focus_ < 0)
			{
				select_only(index);
				focus_ = anchor_ = index;
				return;
			}
			clear_selection();
			set_selected(focus_, true);
			set_selected(index, true);
		}

		int secondary() const
		{
			for (int index = 0; index < count_; ++index)
				if (index != focus_ && contains(index)) return index;
			return -1;
		}

		bool contains(const int index) const
		{
			return valid(index) && selected_[static_cast<size_t>(index)];
		}

		std::vector<int> selected() const
		{
			std::vector<int> result;
			result.reserve(selectedCount_);
			for (size_t index = 0; index < selected_.size(); ++index)
				if (selected_[index]) result.push_back(static_cast<int>(index));
			return result;
		}

		void for_each_selected(const std::function<void(int)>& action) const
		{
			if (!action) return;
			for (size_t index = 0; index < selected_.size(); ++index)
				if (selected_[index]) action(static_cast<int>(index));
		}

		size_t count() const { return selectedCount_; }
		size_t size() const { return selectedCount_; }
		int focus() const { return focus_; }

	private:
		bool valid(const int index) const
		{
			return index >= 0 && index < count_ && static_cast<size_t>(index) < selected_.size();
		}

		void set_selected(const int index, const bool value)
		{
			if (!valid(index) || selected_[static_cast<size_t>(index)] == value) return;
			selected_[static_cast<size_t>(index)] = value;
			if (value) ++selectedCount_;
			else --selectedCount_;
		}

		void clear_selection()
		{
			selected_.assign(selected_.size(), false);
			selectedCount_ = 0;
		}

		void select_only(const int index)
		{
			clear_selection();
			set_selected(index, true);
		}

		void extend_range(const int index, const bool control)
		{
			if (!control) clear_selection();
			const int first = (std::min)(anchor_, index);
			const int last = (std::max)(anchor_, index);
			for (int item = first; item <= last; ++item) set_selected(item, true);
		}

		int count_{};
		int focus_{-1};
		int anchor_{-1};
		size_t selectedCount_{};
		std::vector<bool> selected_;
		std::vector<bool> marqueeBase_;
	};
}
