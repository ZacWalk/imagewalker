// ImageWalker by Zac Walker
// Implements mode-bar presentation, folder shortcuts, and hosting of the task views.

#include "MainWindow.h"

#include "Paths.h"
#include "TaskConvert.h"
#include "TaskEdit.h"
#include "TaskRename.h"
#include "TaskSync.h"

#include <algorithm>
#include <array>
#include <format>

namespace iw
{
	namespace
	{
		constexpr std::wstring_view shortcutSection = L"Shortcuts";
		constexpr size_t maximumShortcuts = 12;

		std::wstring shortcut_caption(const std::filesystem::path& path)
		{
			auto name = path.filename().wstring();
			if (name.empty()) name = path.root_name().wstring();
			return name.empty() ? path.wstring() : name;
		}
	}

	void MainWindow::load_shortcuts()
	{
		shortcuts_.clear();
		std::error_code error;
		for (const auto folder : {
			     platform::KnownFolder::pictures, platform::KnownFolder::documents,
			     platform::KnownFolder::desktop, platform::KnownFolder::downloads
		     })
			if (auto path = platform::known_folder(folder); !path.empty()) shortcuts_.push_back(std::move(path));
		for (size_t index = 0; index < maximumShortcuts; ++index)
		{
			const auto text = platform::read_text_setting(shortcutSection, std::format(L"Path{}", index));
			if (text.empty()) continue;
			std::filesystem::path path(text);
			error.clear();
			if (!std::filesystem::is_directory(files::native_path(path), error)) continue;
			if (std::ranges::any_of(shortcuts_, [&path](const auto& existing)
			{
				return paths::equal(existing, path);
			}))
				continue;
			shortcuts_.push_back(std::move(path));
		}
	}

	void MainWindow::save_shortcuts() const
	{
		// Only the pinned folders are stored; the known folders are rediscovered each run.
		std::vector<std::filesystem::path> pinned;
		for (const auto& path : shortcuts_)
		{
			bool known = false;
			for (const auto folder : {
				     platform::KnownFolder::pictures, platform::KnownFolder::documents,
				     platform::KnownFolder::desktop, platform::KnownFolder::downloads
			     })
				known = known || paths::equal(platform::known_folder(folder), path);
			if (!known) pinned.push_back(path);
		}
		platform::clear_settings_section(shortcutSection);
		std::vector<platform::TextSetting> values;
		values.reserve(pinned.size());
		for (size_t index = 0; index < pinned.size() && index < maximumShortcuts; ++index)
			values.push_back({std::format(L"Path{}", index), pinned[index].wstring()});
		platform::write_text_settings(shortcutSection, values);
	}

	void MainWindow::configure_mode_bar()
	{
		if (!modeBar_) return;
		modeBar_->set_modes({
			{modeBrowse, ModeBar::Glyph::browse, L"Browse", {}, !task_},
			{modeFolders, ModeBar::Glyph::folders, L"Folders", {}, showTree_},
			{modeConvert, ModeBar::Glyph::convert, L"Convert"},
			{modeRename, ModeBar::Glyph::rename, L"Rename"},
			{modeEdit, ModeBar::Glyph::edit, L"Edit"},
			{modeSync, ModeBar::Glyph::sync, L"Sync"},
			{modeFullscreen, ModeBar::Glyph::fullscreen, L"Full", {}, fullscreen_}
		});
		update_mode_bar();
	}

	void MainWindow::update_mode_bar()
	{
		if (!modeBar_) return;
		std::vector<ModeBar::Button> buttons;
		buttons.reserve(shortcuts_.size() + 1);
		int id = shortcutFirst;
		for (const auto& path : shortcuts_)
		{
			ModeBar::Button button;
			button.id = id++;
			button.glyph = ModeBar::Glyph::folder;
			button.caption = shortcut_caption(path);
			button.path = path;
			button.checked = paths::equal(path, folder_);
			button.removable = true;
			buttons.push_back(std::move(button));
		}
		ModeBar::Button pin;
		pin.id = id;
		pin.glyph = ModeBar::Glyph::pin;
		pin.caption = L"Pin";
		pin.enabled = !folder_.empty() && shortcuts_.size() < maximumShortcuts * 2 &&
			std::ranges::none_of(shortcuts_, [this](const auto& existing)
			{
				return paths::equal(existing, folder_);
			});
		buttons.push_back(std::move(pin));
		modeBar_->set_shortcuts(std::move(buttons));

		modeBar_->set_checked(task_ ? task_->mode_id() : modeBrowse);
		modeBar_->set_enabled(modeConvert, has_photo_selection());
		modeBar_->set_enabled(modeRename, canvas_.selection_count() > 0);
		modeBar_->set_enabled(modeEdit, !editable_photo().empty());
		modeBar_->set_enabled(modeSync, true);
	}

	void MainWindow::mode_bar_command(const ModeBar::Button& button)
	{
		switch (button.id)
		{
		case modeBrowse:
			close_task();
			return;

		case modeFolders:
			showTree_ = !showTree_;
			update_visibility();
			configure_mode_bar();
			update_toolbar_state();
			return;

		case modeConvert:
			if (task_ && task_->mode_id() == modeConvert) close_task();
			else open_convert_task();
			return;

		case modeRename:
			if (task_ && task_->mode_id() == modeRename) close_task();
			else open_rename_task();
			return;

		case modeSync:
			if (task_ && task_->mode_id() == modeSync) close_task();
			else open_sync_task();
			return;

		case modeEdit:
			if (task_ && task_->mode_id() == modeEdit) close_task();
			else open_edit_task();
			return;

		case modeFullscreen:
			if (!close_task()) return;
			set_fullscreen(!fullscreen_);
			configure_mode_bar();
			return;

		default:
			break;
		}
		if (!button.path.empty() && close_task()) open_folder(button.path);
		else if (button.glyph == ModeBar::Glyph::pin && !folder_.empty())
		{
			shortcuts_.push_back(folder_);
			save_shortcuts();
			update_mode_bar();
		}
	}

	void MainWindow::remove_shortcut(const ModeBar::Button& button)
	{
		if (button.path.empty()) return;
		const auto removed = std::erase_if(shortcuts_, [&button](const auto& existing)
		{
			return paths::equal(existing, button.path);
		});
		if (!removed) return;
		save_shortcuts();
		update_mode_bar();
	}

	bool MainWindow::has_photo_selection() const
	{
		const auto selected = canvas_.selected_paths();
		return !selected.empty() && std::ranges::all_of(selected, [](const auto& path)
		{
			return files::is_supported_image(path);
		});
	}

	std::vector<std::filesystem::path> MainWindow::photo_selection() const
	{
		auto selected = canvas_.selected_paths();
		if (selected.empty()) return {};
		for (const auto& path : selected)
		{
			std::error_code error;
			if (!files::is_supported_image(path) ||
				!std::filesystem::is_regular_file(files::native_path(path), error) || error) return {};
		}
		return selected;
	}

	std::filesystem::path MainWindow::editable_photo() const
	{
		// Edit is a single-photo task: the focused photo, not a hidden batch target.
		const int focused = canvas_.focused_index();
		if (focused >= 0 && static_cast<size_t>(focused) < files_.size() &&
			files::is_editable_image(files_[static_cast<size_t>(focused)]))
			return files_[static_cast<size_t>(focused)];
		const auto selected = canvas_.selected_paths();
		if (selected.size() == 1 && files::is_editable_image(selected.front())) return selected.front();
		return {};
	}

	bool MainWindow::set_task(TaskViewPtr task)
	{
		if (!close_task()) return false;
		if (!task) return false;
		const auto weak = weak_from_this();
		const auto taskWeak = std::weak_ptr<TaskView>(task);
		TaskView::Host host;
		host.closed = [weak]
		{
			if (const auto self = weak.lock())
				if (self->close_task() && self->closeApplicationAfterTask_) self->frame_->close();
		};
		host.status = [this](std::wstring text)
		{
			statusText_ = std::move(text);
			update_status_surface();
		};
		host.progress = [this](const size_t complete, const size_t total) { update_progress(complete, total); };
		host.refreshChrome = [this] { update_toolbar_state(); };
		host.completed = [weak](const tasks::TaskPlan& plan)
		{
			if (const auto self = weak.lock())
			{
				for (const auto& row : plan.rows)
				{
					if (row.state != tasks::RowState::success && row.state != tasks::RowState::failed) continue;
					self->canvas_.invalidate_file(row.source);
					self->canvas_.invalidate_file(row.destination);
				}
				self->open_folder(self->folder_, false);
			}
		};
		host.saved = [weak, taskWeak](const std::filesystem::path& path, const bool saveAs)
		{
			const auto self = weak.lock();
			const auto savedTask = taskWeak.lock();
			if (!self || !self->frame_->native_handle() ||
				(self->task_ && self->task_ != savedTask)) return;
			self->canvas_.invalidate_file(path);
			if (saveAs)
			{
				if (self->close_task() && self->open_folder(path.parent_path())) self->open_image(path);
			}
			else self->open_folder(self->folder_, false);
		};
		host.toggleMaximize = [weak]
		{
			if (const auto self = weak.lock())
			{
				if (self->fullscreen_) self->set_fullscreen(false);
				self->frame_->set_maximized(!self->frame_->placement().maximized);
				self->update_toolbar_state();
			}
		};
		host.maximized = [weak]
		{
			const auto self = weak.lock();
			return self && self->frame_->placement().maximized;
		};
		if (!task->create(centerFrame_, std::move(host))) return false;
		task_ = std::move(task);
		informationProgressActive_ = false;
		scanning_ = false;
		progressPercent_ = 100;
		task_->set_dpi(dpi_);
		canvas_.frame()->show(false);
		task_->set_bounds(centerFrame_->client_rect());
		task_->show(true);
		if (chrome_) chrome_->set_task_toolbar(task_->toolbar_items());
		update_visibility();
		frame_->set_title(std::format(L"{} - ImageWalker 3.0", task_->title()));
		task_->frame()->set_focus();
		task_->initialize();
		configure_mode_bar();
		update_toolbar_state();
		return true;
	}

	bool MainWindow::close_task()
	{
		if (!task_) return true;
		if (!task_->request_close()) return false;
		auto closing = std::move(task_);
		task_.reset();
		closing->show(false);
		if (chrome_) chrome_->clear_task_toolbar();
		closing->dispose();
		if (canvas_.frame())
		{
			canvas_.frame()->show(true);
			canvas_.frame()->set_focus();
		}
		progressPercent_ = 100;
		update_visibility();
		frame_->set_title(std::format(L"{} - ImageWalker 3.0", folder_.filename().wstring()));
		update_breadcrumb();
		update_selection_status();
		configure_mode_bar();
		update_toolbar_state();
		return true;
	}

	void MainWindow::request_exit()
	{
		if (close_task()) frame_->close();
		else closeApplicationAfterTask_ = task_ && task_->closing();
	}

	void MainWindow::open_convert_task()
	{
		if (canvas_.selection_count() == 0)
		{
			platform::show_error(L"Select one or more photos before converting.", L"Convert or Resize", frame_);
			return;
		}
		auto selected = photo_selection();
		if (selected.empty())
		{
			platform::show_error(
				L"The selection contains a folder, non-photo, missing file, or unreadable file. "
				L"Select only local photo files before converting.",
				L"Convert or Resize", frame_);
			return;
		}
		auto task = std::make_shared<TaskConvert>();
		task->set_mode_id(modeConvert);
		task->set_targets(std::move(selected));
		task->set_initial_destination(folder_);
		set_task(std::move(task));
	}

	void MainWindow::open_rename_task()
	{
		auto selected = canvas_.selected_paths();
		if (selected.empty())
		{
			platform::show_error(L"Select one or more items before renaming.", L"Batch Rename", frame_);
			return;
		}
		auto task = std::make_shared<TaskRename>();
		task->set_mode_id(modeRename);
		task->set_targets(std::move(selected));
		set_task(std::move(task));
	}

	void MainWindow::open_sync_task()
	{
		auto task = std::make_shared<TaskSync>();
		task->set_mode_id(modeSync);
		task->set_local_folder(folder_);
		set_task(std::move(task));
	}

	void MainWindow::open_edit_task()
	{
		const auto photo = editable_photo();
		if (photo.empty())
		{
			platform::show_error(L"Select a photo to edit.", L"Edit", frame_);
			return;
		}
		// The selector strip offers the editable photos of this folder, and nothing else.
		std::vector<std::filesystem::path> editable;
		for (const auto& path : files_)
			if (files::is_editable_image(path)) editable.push_back(path);
		if (editable.empty()) editable.push_back(photo);
		auto task = std::make_shared<TaskEdit>();
		task->set_mode_id(modeEdit);
		task->set_targets(std::move(editable));
		task->set_photo(photo);
		set_task(std::move(task));
	}
}
