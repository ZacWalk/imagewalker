// ImageWalker by Zac Walker
// Models sortable and filterable folder contents, load progress, and load failures independently of Win32 UI controls.

#pragma once

#include "Files.h"
#include "Paths.h"
#include <algorithm>
#include <filesystem>
#include <ranges>
#include <system_error>
#include <vector>

namespace iw
{
	enum class LoadState { empty, loading, ready, failed };

	class CollectionState
	{
	public:
		void clear()
		{
			items_.clear();
			visibleItems_.clear();
			photoCount_ = 0;
			folderCount_ = 0;
			loadState_ = LoadState::empty;
			loadError_ = {};
		}

		void begin_load()
		{
			loadState_ = LoadState::loading;
			loadError_ = {};
		}

		void fail_load(const std::error_code error)
		{
			loadState_ = LoadState::failed;
			loadError_ = error;
		}

		void set_items(std::vector<files::FolderItem> items)
		{
			items_ = std::move(items);
			loadState_ = LoadState::ready;
			loadError_ = {};
			sort();
			filter();
		}

		void set_sort_field(const files::SortField field)
		{
			set_sort(field, sortAscending_);
		}

		void set_sort(const files::SortField field, const bool ascending)
		{
			if (sortField_ == field && sortAscending_ == ascending) return;
			sortField_ = field;
			sortAscending_ = ascending;
			sort();
			filter();
		}

		void set_photos_only(const bool photosOnly)
		{
			if (photosOnly_ == photosOnly) return;
			photosOnly_ = photosOnly;
			filter();
		}

		bool set_dimensions(const std::filesystem::path& path, const int width, const int height)
		{
			const auto match = std::ranges::find_if(items_, [&path](const files::FolderItem& item)
			{
				return paths::equal(item.path, path);
			});
			if (match == items_.end() || (match->width == width && match->height == height)) return false;
			match->width = width;
			match->height = height;
			for (auto& visible : visibleItems_)
				if (paths::equal(visible.path, path))
				{
					visible.width = width;
					visible.height = height;
					break;
				}
			return true;
		}

		const std::vector<files::FolderItem>& items() const { return items_; }
		const std::vector<files::FolderItem>& visible_items() const { return visibleItems_; }
		size_t photo_count() const { return photoCount_; }
		size_t folder_count() const { return folderCount_; }
		bool photos_only() const { return photosOnly_; }
		files::SortField sort_field() const { return sortField_; }
		bool sort_ascending() const { return sortAscending_; }
		LoadState load_state() const { return loadState_; }
		std::error_code load_error() const { return loadError_; }

	private:
		void sort()
		{
			files::sort_items(items_, sortField_, sortAscending_);
			photoCount_ = static_cast<size_t>(std::ranges::count(items_, files::ItemKind::image,
			                                                     &files::FolderItem::kind));
			folderCount_ = static_cast<size_t>(std::ranges::count(items_, files::ItemKind::folder,
			                                                      &files::FolderItem::kind));
		}

		void filter()
		{
			visibleItems_.clear();
			visibleItems_.reserve(items_.size());
			for (const auto& item : items_)
				if (!photosOnly_ || item.kind == files::ItemKind::image) visibleItems_.push_back(item);
		}

		std::vector<files::FolderItem> items_;
		std::vector<files::FolderItem> visibleItems_;
		size_t photoCount_{};
		size_t folderCount_{};
		bool photosOnly_{true};
		bool sortAscending_{true};
		files::SortField sortField_{files::SortField::name};
		LoadState loadState_{LoadState::empty};
		std::error_code loadError_;
	};
}
