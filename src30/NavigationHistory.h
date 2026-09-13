// ImageWalker by Zac Walker
// Models Back / Forward / Up navigation as testable state independent of any window.

#pragma once

#include "Paths.h"
#include <filesystem>
#include <optional>
#include <vector>

namespace iw
{
	class NavigationHistory
	{
	public:
		static constexpr size_t maximumEntries = 128;

		// Records a folder the user actually reached. Repeating the current entry is ignored so
		// refreshing never grows the stack, and any forward branch is discarded.
		void visit(const std::filesystem::path& folder)
		{
			if (folder.empty()) return;
			if (!entries_.empty() && paths::equal(entries_[index_], folder)) return;
			entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index_) + (entries_.empty() ? 0 : 1),
			               entries_.end());
			entries_.push_back(folder);
			if (entries_.size() > maximumEntries)
			{
				entries_.erase(entries_.begin());
			}
			index_ = entries_.size() - 1;
		}

		bool can_go_back() const { return index_ > 0; }
		bool can_go_forward() const { return !entries_.empty() && index_ + 1 < entries_.size(); }

		// Returns the folder to open, having already moved the cursor. Call rollback() when the
		// target turns out to be unreachable so Back / Forward never disagree with the view.
		std::optional<std::filesystem::path> go(const int direction)
		{
			if (direction < 0 && !can_go_back()) return std::nullopt;
			if (direction > 0 && !can_go_forward()) return std::nullopt;
			if (direction == 0) return std::nullopt;
			previous_ = index_;
			index_ = direction < 0 ? index_ - 1 : index_ + 1;
			return entries_[index_];
		}

		void rollback() { index_ = previous_ < entries_.size() ? previous_ : index_; }

		// Removes a folder that no longer exists so the user is not offered it again.
		void forget(const std::filesystem::path& folder)
		{
			for (size_t position = 0; position < entries_.size();)
			{
				if (!paths::equal(entries_[position], folder))
				{
					++position;
					continue;
				}
				entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(position));
				if (index_ > position || (index_ == position && index_ + 1 >= entries_.size()))
					index_ = index_ ? index_ - 1 : 0;
			}
			previous_ = index_;
		}

		const std::filesystem::path& current() const
		{
			static const std::filesystem::path empty;
			return entries_.empty() ? empty : entries_[index_];
		}

		size_t size() const { return entries_.size(); }

	private:
		std::vector<std::filesystem::path> entries_;
		size_t index_{};
		size_t previous_{};
	};

	// Up is only meaningful while a real parent folder exists. std::filesystem reports "C:" as the
	// parent of "C:\", which is a drive-relative path rather than a folder, so it is excluded here.
	inline std::optional<std::filesystem::path> parent_folder(const std::filesystem::path& folder)
	{
		if (folder.empty()) return std::nullopt;
		if (paths::equal(folder, folder.root_path())) return std::nullopt;
		auto parent = folder.parent_path();
		if (parent.empty() || paths::equal(parent, folder)) return std::nullopt;
		return parent;
	}
}
