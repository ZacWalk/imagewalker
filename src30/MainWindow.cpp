// ImageWalker by Zac Walker
// Implements application navigation, command policy, canvas coordination, and persisted window state.

#include "Platform.h"
#include "MainWindow.h"
#include "Files.h"
#include "Format.h"
#include "Paths.h"
#include "Media.h"
#include "Undo.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <memory>
#include <system_error>

namespace iw
{
	namespace
	{
		constexpr int controlAddress = 2001;
		constexpr int controlTree = 2002;
		constexpr platform::DialogFieldId dialogPasteImageFormat = 1;
	}

	bool MainWindow::can_undo_last_action() const
	{
		return task_ ? task_->can_undo_draft() : undoHistory_ && undoHistory_->can_undo();
	}

	void MainWindow::undo_last_action()
	{
		if (task_)
		{
			if (!task_->undo_draft())
			{
				statusText_ = L"This task has no draft edit to undo. Close it to undo a completed file task.";
				update_status_surface();
			}
			return;
		}
		if (!undoHistory_ || !undoHistory_->can_undo())
		{
			statusText_ = L"No completed file task is available to undo.";
			update_status_surface();
			return;
		}
		auto& history = *undoHistory_;
		platform::ChoiceDefinition confirmation;
		confirmation.title = L"Undo file task";
		confirmation.heading = L"Undo " + history.description() + L"?";
		confirmation.message =
			L"Restore the previous file state for this task's completed changes. "
			L"Files changed outside ImageWalker are refused, not overwritten. Recovery copies remain on disk.";
		confirmation.buttons = {{1, L"Undo"}, {2, L"Cancel"}};
		confirmation.defaultButton = 2;
		confirmation.warning = true;
		if (platform::show_choice(frame_, confirmation) != 1) return;

		canvas_.stop_media();
		undo::Report report;
		const bool processed = platform::run_with_status(frame_, L"Undo file task", L"Restoring previous file state...",
		                                                 [&]
		{
			report = history.undo();
			return true;
		});
		canvas_.invalidate_all_files();
		auto refresh = folder_;
		while (!refresh.empty())
		{
			std::error_code error;
			if (std::filesystem::is_directory(files::native_path(refresh), error) && !error) break;
			const auto parent = parent_folder(refresh);
			if (!parent)
			{
				refresh.clear();
				break;
			}
			refresh = *parent;
		}
		if (!refresh.empty()) open_folder(refresh, false);
		if (shellTree_) shellTree_->refresh();
		if (!processed)
		{
			platform::show_error(L"Undo stopped with an error. Check the refreshed files and recovery data in:\n" +
			                         history.root().wstring(),
			                     L"Undo file task", frame_);
			return;
		}
		platform::write_diagnostic(L"Undo: " + report.summary() + L"\n");
		statusText_ = std::format(L"Undo: {} paths restored, {} conflicts, {} failures.", report.restored,
		                          report.conflicts, report.failed);
		update_status_surface();
		platform::DialogDefinition results;
		results.title = L"Undo results";
		results.message = statusText_;
		const size_t shown = (std::min)(report.details.size(), size_t{5});
		for (size_t i = 0; i < shown; ++i) results.message += L"\n\n" + report.details[i];
		if (shown != report.details.size()) results.message += L"\n\nSee the diagnostic log for the remaining details.";
		results.message += L"\n\nRecovery copies: " + history.root().wstring();
		platform::show_modal_dialog(frame_, std::move(results));
	}

	bool MainWindow::create(const int showCommand, const std::filesystem::path& initialPath)
	{
		load_settings();
		platform::WindowOptions options;
		options.className = "ImageWalker30.MainWindow";
		options.title = L"ImageWalker 3.0";
		options.bounds = savedWindowBounds_;
		options.useDefaultPosition = !hasSavedWindowPosition_;
		options.showCommand = showCommand;
		options.maximized = startMaximized_;
		options.icon = platform::WindowIcon::application;
		frame_ = platform::create_top_level_frame(shared_from_this(), options);
		if (!frame_) return false;

		open_startup_folder(initialPath);
		return true;
	}

	void MainWindow::open_startup_folder(const std::filesystem::path& initialPath)
	{
		if (!initialPath.empty())
		{
			std::error_code error;
			const auto absolute = std::filesystem::absolute(initialPath, error);
			if (!error && open_folder(absolute)) return;
			if (!error && std::filesystem::is_regular_file(files::native_path(absolute), error) &&
			    open_folder(absolute.parent_path(), true, true, absolute))
				return;
			platform::write_diagnostic(L"Could not open the requested startup path.\n");
		}
		if (!lastFolder_.empty() && open_folder(lastFolder_))
		{
			rememberedScan_ = folderScanGeneration_.load();
			return;
		}
		if (!open_pictures_folder())
			platform::write_diagnostic(L"Could not open the Pictures, Desktop or current folder at startup.\n");
	}

	bool MainWindow::open_pictures_folder()
	{
		for (const auto folder : {platform::KnownFolder::pictures, platform::KnownFolder::desktop})
			if (const auto path = platform::known_folder(folder); !path.empty() && open_folder(path)) return true;
		std::error_code error;
		const auto current = std::filesystem::current_path(error);
		return !error && open_folder(current);
	}

	bool MainWindow::escape_active_view()
	{
		const auto task = task_;
		if (!task) return false;
		task->escape();
		return true;
	}

	bool MainWindow::view_shortcut(const char32_t key, const bool control, const bool shift, const bool alt)
	{
		namespace keys = ::platform::keys;
		if (alt) return false;
		if (control && !shift && key == keys::F4)
		{
			if (!escape_active_view() && !canvas_.back_out()) request_exit();
			return true;
		}
		if (task_) return false;
		if (control && shift && key == keys::SPACE)
		{
			set_fullscreen(!fullscreen_);
			return true;
		}
		if (!control && !shift && key == keys::BACK)
		{
			navigate_history(-1);
			return true;
		}
		return false;
	}

	platform::MessageResult MainWindow::message(const platform::WindowFramePtr& frame,
	                                            const platform::WindowMessage message)
	{
		if (frame_ && frame != frame_) return 0;
		frame_ = frame;
		switch (message)
		{
		case platform::WindowMessage::create: return on_create() ? 0 : -1;
		case platform::WindowMessage::close: request_exit(); return 0;
		case platform::WindowMessage::dpiChanged: update_dpi(frame_->dpi()); return 0;
		case platform::WindowMessage::focusGained:
			// The frame itself takes no keys; the active task or the canvas does.
			if (task_ && task_->frame()) task_->frame()->set_focus();
			else if (canvas_.frame()) canvas_.frame()->set_focus();
			return 0;
		case platform::WindowMessage::captureLost:
			draggingTreeSplitter_ = false;
			frame_->invalidate(treeSplitterBounds_);
			return 0;
		case platform::WindowMessage::destroy:
			++folderScanGeneration_;
			if (task_ && task_->frame()) task_->frame()->set_reactor({});
			if (modeBar_ && modeBar_->frame()) modeBar_->frame()->set_reactor({});
			if (canvas_.frame()) canvas_.frame()->set_reactor({});
			shellTree_.reset();
			chrome_.reset();
			save_settings();
			return 0;
		default: return 0;
		}
	}

	platform::MessageResult MainWindow::mouse(const platform::WindowFramePtr& frame,
	                                          const platform::MouseMessage message, const platform::MouseInput& input)
	{
		if (frame != frame_) return 0;
		const bool over = treeSplitterBounds_.contains(input.point);
		if (draggingTreeSplitter_ || over) frame->set_cursor(platform::CursorShape::sizeHorizontal);
		if (message == platform::MouseMessage::move || message == platform::MouseMessage::leave)
		{
			const bool hover = message == platform::MouseMessage::move && over;
			if (hover != hoveringTreeSplitter_)
			{
				hoveringTreeSplitter_ = hover;
				if (hover) frame->track_mouse_leave();
				frame->invalidate(treeSplitterBounds_);
			}
		}
		if (over && message == platform::MouseMessage::leftButtonDown)
		{
			draggingTreeSplitter_ = true;
			treeSplitterDragOffset_ = input.point.x - treeSplitterBounds_.x;
			frame->set_capture();
			frame->invalidate(treeSplitterBounds_);
		}
		else if (message == platform::MouseMessage::move && draggingTreeSplitter_)
		{
			const int modes = modeBar_ && showModeBar_ ? modeBar_->width() : 0;
			const int width = (std::max)(0, input.point.x - contentBounds_.x - modes - treeSplitterDragOffset_);
			folderTreeWidth_ = std::clamp(ui::unscale_metric(width, dpi_), 140, 1000);
			on_size();
		}
		else if (message == platform::MouseMessage::leftButtonUp && draggingTreeSplitter_)
		{
			draggingTreeSplitter_ = false;
			frame->release_capture();
			frame->invalidate(treeSplitterBounds_);
		}
		return 0;
	}

	void MainWindow::paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw)
	{
		if (frame == frame_ && treeSplitterBounds_.width > 0)
			draw.fill(treeSplitterBounds_,
			          platform::calc_hande_color(hoveringTreeSplitter_, draggingTreeSplitter_, false));
	}

	void MainWindow::size(const platform::WindowFramePtr& frame, const app::sizei, platform::MeasureContext&)
	{
		if (frame_ && frame != frame_) return;
		frame_ = frame;
		on_size();
	}

	bool MainWindow::on_create()
	{
		update_dpi(frame_->dpi());
		chrome_ = platform::create_command_surface(frame_);
		configure_chrome();

		modeBar_ = std::make_shared<ModeBar>();
		if (!modeBar_->create(frame_)) return false;
		modeBar_->set_command_handler([this](const ModeBar::Button& button) { mode_bar_command(button); });
		modeBar_->set_remove_handler([this](const ModeBar::Button& button) { remove_shortcut(button); });
		shellTree_ = platform::create_shell_tree(frame_, {[this](const auto& path)
		{ open_folder(path, true, false); }, [this] { open_folder(folder_, false); }});
		if (!chrome_ || !shellTree_ || !canvas_.create(frame_))
			return false;
		canvas_.set_dpi(dpi_);
		canvas_.set_activation_handler([this](const auto& path) { activate_item(path); });
		canvas_.set_clear_filter_handler([this]
		{
			collection_.set_photos_only(false);
			apply_collection_view();
		});
		canvas_.set_selection_handler([this] { update_selection_status(); });
		canvas_.set_zoom_mode_handler([this](const bool zoomed)
		{
			if (zoomMode_ == zoomed) return;
			zoomMode_ = zoomed;
			update_visibility();
		});
		canvas_.set_command_handlers({[this] { set_fullscreen(true); }, [this] { set_fullscreen(false); },
		                              [this]
		{
			canvas_.set_file_properties_visible(!canvas_.file_properties_visible());
			update_fullscreen_menu_checks();
		}, [this]
		{
			canvas_.set_thumbnail_selector_visible(!canvas_.thumbnail_selector_visible());
			update_fullscreen_menu_checks();
		}, [this](const int value) { set_thumbnail_size(value); }, [this] { open_with_chooser(); }});
		canvas_.set_sort_handler([this](const files::SortField field, const bool ascending)
		{
			collection_.set_sort(field, ascending);
			apply_collection_view();
			probe_dimensions();
		});
		canvas_.set_context_menu_handler([this](const app::pointi point) { show_file_menu(point); });
		canvas_.set_drop_handler([this](const std::vector<std::filesystem::path>& paths)
		{ import_dropped_files(paths); });
		canvas_.set_progress_handler([this](const size_t complete, const size_t total)
		{
			if (!task_) update_progress(complete, total);
		});
		canvas_.set_load_error_handler([this](const std::filesystem::path&)
		{
			platform::show_error(L"ImageWalker could not decode this image. The file may be damaged or unsupported.",
			                     L"ImageWalker 3.0", frame_);
		});
		canvas_.set_information_progress_handler([this](const size_t complete, const size_t total)
		{ update_information_progress(complete, total); });
		canvas_.set_file_drag_handler([this](auto paths)
		{
			if (platform::begin_file_drag(frame_, paths)) open_folder(folder_, false);
		});
		canvas_.frame()->accept_file_drops();
		canvas_.set_splitter_position(savedSplitterPosition_);
		set_thumbnail_size(thumbnailSize_);
		set_view_mode(viewMode_);
		canvas_.set_fit(fitImage_);
		load_shortcuts();
		configure_mode_bar();
		update_visibility();
		shellTree_->set_current_path(folder_);

		// Tool discovery walks the filesystem, so it never runs on the UI thread.
		const auto weak = weak_from_this();
		platform::queue_work(platform::WorkQueue::shell, [weak]
		{
			const auto exiting = platform::exit_token();
			tools::ToolTable table;
			table.load(platform::module_folder() / L"imagewalker30-tools.json", exiting);
			if (exiting.stop_requested()) return;
			platform::queue_ui([weak, table = std::move(table)]() mutable
			{
				if (const auto self = weak.lock())
				{
					self->tools_ = std::move(table);
					if (!self->tools_.issues().empty())
					{
						self->statusText_ = L"Tool configuration needs attention. Open Help > Tool configuration.";
						self->update_status_surface();
					}
				}
			});
		});

		return true;
	}

	int MainWindow::scale_metric(const int value) const
	{
		return (value * static_cast<int>(dpi_) + 48) / 96;
	}

	void MainWindow::update_dpi(const unsigned int dpi)
	{
		dpi_ = dpi ? dpi : 96;
		if (chrome_) chrome_->set_dpi(dpi_);
		if (canvas_.frame()) canvas_.set_dpi(dpi_);
		if (modeBar_) modeBar_->set_dpi(dpi_);
		if (task_) task_->set_dpi(dpi_);
	}

	void MainWindow::configure_chrome()
	{
		if (!chrome_) return;
		const auto canvasHasSelection = [this]
		{
			const auto context = build_command_context();
			return context.focus == FocusTarget::canvas && context.selectionCount > 0;
		};
		const auto canvasHasOneSelection = [this]
		{
			const auto context = build_command_context();
			return context.focus == FocusTarget::canvas && context.selectionCount == 1;
		};
		const auto command = [](std::wstring name, std::function<void()> invoke, platform::Shortcut shortcut = {},
		                        std::wstring tooltip = {},
		                        const platform::CommandBitmap bitmap = platform::CommandBitmap::none,
		                        std::function<bool()> enabled = {}, std::function<bool()> checked = {})
		{
			auto result = std::make_shared<platform::Command>();
			result->bitmap = bitmap;
			result->name = std::move(name);
			result->shortcut = shortcut;
			result->tooltip = std::move(tooltip);
			result->invoke = std::move(invoke);
			result->enabled = std::move(enabled);
			result->checked = std::move(checked);
			return result;
		};

		using enum platform::ShortcutKey;
		const auto open = command(L"&Open...", [this] { open_file_picker(); }, {o, true}, L"Open image");
		const auto exit = command(L"E&xit", [this] { request_exit(); });
		const auto copy = command(L"&Copy", [this] { copy_selected(false); }, {c, true}, L"Copy", {},
		                          canvasHasSelection);
		const auto cut = command(L"Cu&t", [this] { copy_selected(true); }, {x, true}, L"Cut", {}, canvasHasSelection);
		const auto paste = command(L"&Paste", [this] { paste_files(); }, {v, true}, L"Paste", {}, [this]
		{
			const auto context = build_command_context();
			return context.hasFolder && (context.clipboardHasFiles || context.clipboardHasImage);
		});
		const auto selectAll = command(L"Select &all",
		                               [this]
		{
			canvas_.select_all();
			update_selection_status();
		}, {a, true}, {}, {}, [this]
		{
			const auto context = build_command_context();
			return context.focus == FocusTarget::canvas && context.hasFolder && !context.fullscreen;
		});
		const auto rename = command(L"&Rename", [this] { rename_selected(); }, {f2}, {}, {}, canvasHasOneSelection);
		const auto deleteItem = command(L"&Delete", [this] { delete_selected(); }, {deleteKey}, {}, {},
		                                canvasHasSelection);
		const auto refresh = command(L"&Refresh", [this]
		{
			if (open_folder(folder_, false)) canvas_.invalidate_all_files();
		}, {f5}, L"Refresh", platform::CommandBitmap::refresh, [this] { return !folder_.empty(); });
		const auto fit = command(L"&Fit image", [this] { canvas_.set_fit(true); }, {}, L"Fit image",
		                         platform::CommandBitmap::fit, [this] { return canvas_.can_zoom(); },
		                         [this] { return canvas_.fit(); });
		const auto zoomIn = command(L"Zoom &in", [this] { canvas_.zoom(1.25); }, {}, L"Zoom in",
		                            platform::CommandBitmap::zoomIn, [this] { return canvas_.can_zoom(); });
		const auto zoomOut = command(L"Zoom &out", [this] { canvas_.zoom(0.8); }, {}, L"Zoom out",
		                             platform::CommandBitmap::zoomOut, [this] { return canvas_.can_zoom(); });
		const auto tree = command(L"Folder &tree", [this]
		{
			showTree_ = !showTree_;
			update_visibility();
			update_toolbar_state();
		}, {f4, false, true}, L"Show folder tree", platform::CommandBitmap::folderTree, {}, [this] { return showTree_; });
		const auto fullscreen = command(L"&Full screen", [this] { set_fullscreen(!fullscreen_); }, {f11},
		                                L"Full screen", {}, [this] { return !task_; }, [this] { return fullscreen_; });
		const auto modeStrip = command(L"&Mode bar", [this]
		{
			showModeBar_ = !showModeBar_;
			update_visibility();
			update_toolbar_state();
		}, {}, {}, {}, {}, [this] { return showModeBar_; });
		const auto details = command(L"&Details", [this] {
			set_view_mode(ViewMode::details);
		}, {}, L"Details", platform::CommandBitmap::details, {}, [this] { return viewMode_ == ViewMode::details; });
		const auto thumbnails = command(L"&Thumbnails", [this] { set_view_mode(ViewMode::thumbnails); }, {},
		                                L"Thumbnails", platform::CommandBitmap::thumbnails, {},
		                                [this] { return viewMode_ == ViewMode::thumbnails; });
		const auto matrix = command(L"&Matrix", [this] {
			set_view_mode(ViewMode::matrix);
		}, {}, L"Matrix", platform::CommandBitmap::matrix, {}, [this] { return viewMode_ == ViewMode::matrix; });
		const auto fullscreenProperties = command(L"Full screen file &properties", [this]
		{
			canvas_.set_file_properties_visible(!canvas_.file_properties_visible());
			update_fullscreen_menu_checks();
		}, {}, {}, {}, {}, [this] { return canvas_.file_properties_visible(); });
		const auto fullscreenThumbnails = command(L"Full screen &thumbnail selector", [this]
		{
			canvas_.set_thumbnail_selector_visible(!canvas_.thumbnail_selector_visible());
			update_fullscreen_menu_checks();
		}, {}, {}, {}, {}, [this] { return canvas_.thumbnail_selector_visible(); });
		const auto photosOnly = command(L"Show &photos only", [this]
		{
			collection_.set_photos_only(!collection_.photos_only());
			apply_collection_view();
		}, {}, L"Show photos only", platform::CommandBitmap::photosOnly, {}, [this] {
			return collection_.photos_only();
		});
		photosOnly->toolbarText = [this]
		{
			return std::format(L"{} images", collection_.photo_count());
		};
		const auto sortName = command(L"&Name", [this] { set_sort_field(files::SortField::name); }, {}, {}, {}, {},
		                              [this] { return collection_.sort_field() == files::SortField::name; });
		const auto sortModified = command(L"Date &modified", [this] {
			set_sort_field(files::SortField::modified);
		}, {}, {}, {}, {}, [this] { return collection_.sort_field() == files::SortField::modified; });
		const auto sortType = command(L"&Type", [this] { set_sort_field(files::SortField::type); }, {}, {}, {}, {},
		                              [this] { return collection_.sort_field() == files::SortField::type; });
		const auto sortSize = command(L"&Size", [this] { set_sort_field(files::SortField::size); }, {}, {}, {}, {},
		                              [this] { return collection_.sort_field() == files::SortField::size; });
		const auto sortDimensions = command(L"&Dimensions", [this] {
			set_sort_field(files::SortField::dimensions);
		}, {}, {}, {}, {}, [this] { return collection_.sort_field() == files::SortField::dimensions; });
		const auto sortDuration = command(L"D&uration", [this] {
			set_sort_field(files::SortField::duration);
		}, {}, {}, {}, {}, [this] { return collection_.sort_field() == files::SortField::duration; });
		const auto thumbnail64 = command(L"&Small (64 px)", [this] { set_thumbnail_size(64); }, {}, {}, {}, {},
		                                 [this] { return thumbnailSize_ == 64; });
		const auto thumbnail128 = command(L"&Medium (128 px)", [this] { set_thumbnail_size(128); }, {}, {}, {}, {},
		                                  [this] { return thumbnailSize_ == 128; });
		const auto thumbnail256 = command(L"&Large (256 px)", [this] { set_thumbnail_size(256); }, {}, {}, {}, {},
		                                  [this] { return thumbnailSize_ == 256; });
		const auto options = command(L"&Options...", [this] { show_options(); }, {f6});
		const auto undoCommand = command(L"&Undo", [this] { undo_last_action(); }, {z, true},
		                                 L"Undo the current photo draft or the last completed file task",
		                                 platform::CommandBitmap::none, [this] { return can_undo_last_action(); });
		const auto convert = command(L"&Convert or Resize...", [this] { open_convert_task(); }, {f8, true}, {}, {},
		                             [this] { return !task_active() && has_photo_selection(); });
		const auto batchRename = command(L"&Batch Rename...", [this] { open_rename_task(); }, {r, true, true}, {}, {},
		                                 [this] { return !task_active() && canvas_.selection_count() > 0; });
		const auto synchronize = command(L"&Synchronize...", [this] { open_sync_task(); }, {f9, true, true}, {}, {},
		                                 [this] { return !task_active(); });
		const auto editPhoto = command(L"&Edit photo", [this] { open_edit_task(); }, {e}, {}, {},
		                               [this] { return !task_active() && !editable_item().empty(); });
		const auto copyTo = command(L"Cop&y to folder...", [this] { copy_or_move_to_folder(false); }, {c, true, true},
		                            {}, {}, canvasHasSelection);
		const auto moveTo = command(L"&Move to folder...", [this] { copy_or_move_to_folder(true); }, {x, true, true},
		                            {}, {}, canvasHasSelection);
		const auto openWith = command(L"Open &with...", [this] { open_with_chooser(); }, {enterKey, true}, {}, {},
		                              canvasHasSelection);
		const auto openWithMenu = command(L"Open with apps and tools", [this]
		{ show_open_with_menu(frame_->client_to_screen({0, 0})); }, {}, {}, {}, canvasHasSelection);
		const auto back = command(L"Back", [this] { navigate_history(-1); }, {left, false, false, true}, L"Back",
		                          platform::CommandBitmap::back, [this] { return history_.can_go_back(); });
		const auto forward = command(L"Forward", [this] { navigate_history(1); }, {right, false, false, true}, L"Forward",
		                             platform::CommandBitmap::forward, [this] { return history_.can_go_forward(); });
		const auto parent = command(L"Parent folder", [this]
		{
			const auto path = folder_.parent_path();
			if (!path.empty() && path != folder_ && !open_folder(path))
				platform::show_error(L"The parent folder could not be opened.", L"ImageWalker 3.0", frame_);
		}, {up, false, false, true}, {}, {}, [this] { return !folder_.empty() && folder_.has_relative_path(); });
		const auto openFolder = command(L"Open folder", [this] { open_folder_picker(); }, {}, L"Open folder",
		                                platform::CommandBitmap::openFolder);
		const auto userGuide = command(L"&User guide", [this] { platform::show_help(frame_); }, {f1});
		const auto about = command(L"&About ImageWalker 3.0", [this] { show_about(); }, {}, L"About",
		                           platform::CommandBitmap::about);
		const auto toolConfiguration = command(L"&Tool configuration...", [this]
		{
			platform::DialogDefinition dialog;
			dialog.title = L"Tool configuration";
			dialog.message = tools_.issue_summary();
			dialog.acceptText = L"Close";
			dialog.cancelText.clear();
			platform::show_modal_dialog(frame_, std::move(dialog));
		});
		const auto sortMenu = command(L"Sort items", [] {}, {}, L"Sort items", platform::CommandBitmap::sort);
		sortMenu->toolbarText = [this]
		{
			switch (collection_.sort_field())
			{
			case files::SortField::name: return std::wstring(L"By name");
			case files::SortField::modified: return std::wstring(L"By date modified");
			case files::SortField::type: return std::wstring(L"By type");
			case files::SortField::size: return std::wstring(L"By size");
			case files::SortField::duration: return std::wstring(L"By duration");
			default: return std::wstring(L"By dimensions");
			}
		};
		// clang-format off
		for (const auto& item : {
			open, copy, cut, paste, selectAll, rename, deleteItem, refresh, fit, zoomIn, zoomOut,
			details, thumbnails, matrix, fullscreenProperties, fullscreenThumbnails, photosOnly,
			sortName, sortModified, sortType, sortSize, sortDimensions, sortDuration, thumbnail64, thumbnail128,
			thumbnail256, copyTo, moveTo, openWith, openWithMenu, back, forward, parent, openFolder, sortMenu})
		// clang-format on
		{
			item->enabled = [this, enabled = item->enabled]
			{
				return !task_ && (!enabled || enabled());
			};
		}
		using platform::MenuItem;
		const std::vector sortItems{MenuItem::action(sortName),       MenuItem::action(sortModified),
		                            MenuItem::action(sortType),       MenuItem::action(sortSize),
		                            MenuItem::action(sortDimensions), MenuItem::action(sortDuration)};
		chrome_->set_menu(
			{{L"&File",
		      {MenuItem::action(open), MenuItem::action(openWith), MenuItem::action(openWithMenu),
		       MenuItem::separator(), MenuItem::action(copyTo), MenuItem::action(moveTo), MenuItem::separator(),
		       MenuItem::action(exit)}},
		     {L"&Edit",
		      {MenuItem::action(undoCommand), MenuItem::separator(), MenuItem::action(copy), MenuItem::action(cut),
		       MenuItem::action(paste), MenuItem::separator(), MenuItem::action(selectAll), MenuItem::action(rename),
		       MenuItem::action(deleteItem)}},
		     {L"&View",
		      {MenuItem::submenu(L"&Go to", {MenuItem::action(back), MenuItem::action(forward), MenuItem::action(parent)}),
		       MenuItem::action(refresh), MenuItem::action(fullscreen), MenuItem::action(fit),
		       MenuItem::action(zoomIn), MenuItem::action(zoomOut),
		       MenuItem::separator(), MenuItem::action(tree), MenuItem::action(modeStrip), MenuItem::separator(),
		       MenuItem::action(details), MenuItem::action(thumbnails), MenuItem::action(matrix), MenuItem::separator(),
		       MenuItem::action(fullscreenProperties), MenuItem::action(fullscreenThumbnails),
		       MenuItem::action(photosOnly), MenuItem::submenu(L"S&ort by", sortItems),
		       MenuItem::submenu(L"Thumbnail si&ze", {MenuItem::action(thumbnail64), MenuItem::action(thumbnail128),
		                                              MenuItem::action(thumbnail256)}),
		       MenuItem::separator(), MenuItem::action(options)}},
		     {L"&Tools",
		      {MenuItem::action(editPhoto), MenuItem::separator(), MenuItem::action(convert),
		       MenuItem::action(batchRename), MenuItem::separator(), MenuItem::action(synchronize)}},
		     {L"&Help",
		      {MenuItem::action(userGuide), MenuItem::action(toolConfiguration), MenuItem::separator(),
		       MenuItem::action(about)}}});
		chrome_->set_toolbar({platform::ToolbarItem::action(back), platform::ToolbarItem::action(forward),
		                      platform::ToolbarItem::action(refresh), platform::ToolbarItem::action(tree),
		                      platform::ToolbarItem::action(openFolder), platform::ToolbarItem::action(about)});
		chrome_->set_status_toolbar({platform::ToolbarItem::action(zoomOut), platform::ToolbarItem::action(fit),
		                             platform::ToolbarItem::action(zoomIn), platform::ToolbarItem::separator(),
		                             platform::ToolbarItem::action(photosOnly),
		                             platform::ToolbarItem::menu(sortMenu, sortItems),
		                             platform::ToolbarItem::separator(), platform::ToolbarItem::action(details),
		                             platform::ToolbarItem::action(thumbnails), platform::ToolbarItem::action(matrix)});
		chrome_->set_thumbnail_size(thumbnailSize_, [this](const int size) { set_thumbnail_size(size); });
		chrome_->set_dpi(dpi_);
	}

	void MainWindow::on_size()
	{
		if (!chrome_ || !canvas_.frame()) return;
		const app::recti content = chrome_->layout(frame_->client_rect(), !task_ && (fullscreen_ || zoomMode_));
		contentBounds_ = content;
		const int contentWidth = (std::max)(0, content.width);
		const int paneHeight = (std::max)(0, content.height);
		const bool showModes = !fullscreen_ && !zoomMode_ && !task_ && showModeBar_;
		const int modeWidth = showModes && modeBar_ ? modeBar_->width() : 0;
		if (modeBar_ && modeBar_->frame())
		{
			modeBar_->frame()->show(showModes);
			modeBar_->frame()->move({content.x, content.y, modeWidth, paneHeight});
		}
		const bool showTreePane = !fullscreen_ && showTree_ && !zoomMode_ && !task_;
		const int splitterWidth = showTreePane ? scale_metric(6) : 0;
		const int available = (std::max)(0, contentWidth - modeWidth);
		const int maximumTreeWidth = (std::max)(0, available - scale_metric(260) - splitterWidth);
		const int minimumTreeWidth = (std::min)(scale_metric(140), maximumTreeWidth);
		const int treeWidth = showTreePane
		                          ? std::clamp(scale_metric(folderTreeWidth_), minimumTreeWidth, maximumTreeWidth)
		                          : 0;
		const int centerLeft = modeWidth + treeWidth + splitterWidth;
		if (shellTree_) shellTree_->set_bounds({content.x + modeWidth, content.y, treeWidth, paneHeight});
		const auto previousSplitter = treeSplitterBounds_;
		treeSplitterBounds_ = {content.x + modeWidth + treeWidth, content.y, splitterWidth, paneHeight};
		if (previousSplitter.width > 0) frame_->invalidate(previousSplitter);
		if (splitterWidth > 0) frame_->invalidate(treeSplitterBounds_);
		centerBounds_ = {content.x + centerLeft, content.y, contentWidth - centerLeft, paneHeight};

		layout_center();
		if (task_) update_toolbar_state();
	}

	bool MainWindow::open_folder(const std::filesystem::path& path, const bool addHistory, const bool synchronizeTree,
	                             const std::filesystem::path& initialItem)
	{
		if (path.empty()) return false;
		// NOLINTNEXTLINE(performance-unnecessary-copy-initialization): path may alias state that close_task() changes.
		const auto requested = path;
		if (task_ && (addHistory || !paths::equal(requested, folder_)) && !close_task()) return false;
		std::error_code error;
		if (!std::filesystem::is_directory(files::native_path(requested), error)) return false;
		auto resolvedPath = std::filesystem::absolute(requested, error).lexically_normal();
		if (error) resolvedPath = requested.lexically_normal();
		const bool preserveView = !addHistory && paths::equal(folder_, resolvedPath);
		folder_ = std::move(resolvedPath);
		if (!task_) update_breadcrumb();
		update_mode_bar();
		if (shellTree_ && synchronizeTree) shellTree_->set_current_path(folder_);
		const auto weak = weak_from_this();
		if (!preserveView)
		{
			files_.clear();
			collection_.clear();
			canvas_.clear_preview();
			canvas_.set_files({});
		}
		collection_.begin_load();
		if (addHistory) history_.visit(folder_);
		const auto title = std::format(L"{} - ImageWalker 3.0", folder_.filename().wstring());
		if (!task_)
		{
			frame_->set_title(title);
			statusText_ = L"Scanning folder...";
			scanning_ = true;
			update_status_surface();
		}
		update_toolbar_state();

		const auto generation = ++folderScanGeneration_;
		rememberedScan_ = 0;
		platform::queue_work(platform::WorkQueue::folder, [this, weak, generation, folder = folder_, initialItem]
		{
			// The progress callback doubles as the cancellation check for a superseded scan.
			std::stop_source cancellation;
			std::error_code scanError;
			auto items = files::scan_folder_items(folder, cancellation.get_token(),
			                                      [this, weak, generation, &cancellation](const size_t entries,
			                                                                              const size_t images)
			{
				if (generation != folderScanGeneration_)
				{
					cancellation.request_stop();
					return;
				}
				platform::queue_ui([weak, generation, entries, images]
				{
					if (const auto self = weak.lock()) self->report_scan_progress(generation, entries, images);
				});
			},
			                                      &scanError);
			if (cancellation.stop_requested()) return;
			platform::queue_ui([weak, generation, folder, scanError, initialItem, items = std::move(items)]() mutable
			{
				if (const auto self = weak.lock())
					self->finish_folder_scan(generation, folder, std::move(items), scanError, initialItem);
			});
		});
		return true;
	}

	void MainWindow::report_scan_progress(const std::uint64_t generation, const size_t entries, const size_t images)
	{
		if (generation != folderScanGeneration_ || task_) return;
		statusText_ = std::format(L"Scanning folder: {} entries, {} images", entries, images);
		update_status_surface();
	}

	void MainWindow::finish_folder_scan(const std::uint64_t generation, const std::filesystem::path& folder,
	                                    std::vector<files::FolderItem> items, const std::error_code error,
	                                    const std::filesystem::path& initialItem)
	{
		if (generation != folderScanGeneration_ || !paths::equal(folder, folder_)) return;
		scanning_ = false;
		if (error)
		{
			if (rememberedScan_ == generation)
			{
				rememberedScan_ = 0;
				history_ = {};
				platform::write_diagnostic(L"Could not read the last folder; trying Pictures.\n");
				if (open_pictures_folder()) return;
			}
			collection_.fail_load(error);
			progressPercent_ = 100;
			update_selection_status();
			return;
		}
		rememberedScan_ = 0;
		lastFolder_ = folder;
		collection_.set_items(std::move(items));
		apply_collection_view();
		if (!initialItem.empty())
		{
			const auto found = std::ranges::find_if(files_, [&initialItem](const auto& file)
			{ return paths::equal(file, initialItem); });
			if (found != files_.end())
			{
				canvas_.select_index(static_cast<int>(found - files_.begin()), false, false);
				open_image(initialItem);
			}
			else if (std::ranges::any_of(collection_.items(), [&initialItem](const files::FolderItem& item)
			         { return media::is_media(item.kind) && paths::equal(item.path, initialItem); }))
				open_image(initialItem);
			else
			{
				statusText_ = L"The requested file is not in this view. Check the current filters.";
				update_status_surface();
			}
		}
		probe_dimensions();
	}

	void MainWindow::open_image(const std::filesystem::path& path)
	{
		if (media::is_media(files::classify(path)))
		{
			canvas_.play_media(path);
			return;
		}
		canvas_.load(path);
	}

	void MainWindow::activate_item(const std::filesystem::path& path)
	{
		const auto& items = collection_.items();
		const auto item = std::ranges::find(items, path, &files::FolderItem::path);
		if (item != items.end() && item->kind == files::ItemKind::folder) open_folder(path);
		else open_image(path);
	}

	void MainWindow::apply_collection_view()
	{
		const auto& visibleItems = collection_.visible_items();
		files_.clear();
		files_.reserve(visibleItems.size());
		for (const auto& item : visibleItems) files_.push_back(item.path);
		canvas_.set_sort(collection_.sort_field(), collection_.sort_ascending());
		canvas_.set_items(visibleItems, collection_.items().size());
		update_collection_controls();
		update_selection_status();
	}

	void MainWindow::set_sort_field(const files::SortField field)
	{
		collection_.set_sort_field(field);
		apply_collection_view();
		probe_dimensions();
	}

	void MainWindow::probe_dimensions()
	{
		if (viewMode_ != ViewMode::details && collection_.sort_field() != files::SortField::dimensions) return;
		if (collection_.load_state() != LoadState::ready) return;
		const std::uint64_t generation = folderScanGeneration_;
		if (dimensionProbeGeneration_ == generation) return;
		dimensionProbeGeneration_ = generation;
		std::vector<std::filesystem::path> paths;
		for (const auto& item : collection_.items())
			if (item.kind == files::ItemKind::image && (item.width <= 0 || item.height <= 0))
				paths.push_back(item.path);
		if (!paths.empty()) pump_dimensions(generation, std::move(paths), 0);
	}

	void MainWindow::pump_dimensions(const std::uint64_t generation, std::vector<std::filesystem::path> paths,
	                                 const size_t offset)
	{
		struct Found
		{
			size_t index{};
			int width{};
			int height{};
		};
		const auto weak = weak_from_this();
		// Small chunks on the metadata queue let a selection's properties interleave with the probe.
		platform::queue_work(platform::WorkQueue::metadata,
		                     [weak, generation, paths = std::move(paths), offset]() mutable
		{
			constexpr size_t chunk = 16;
			const size_t end = (std::min)(offset + chunk, paths.size());
			std::vector<Found> found;
			for (size_t index = offset; index < end; ++index)
			{
				int width = 0, height = 0;
				try
				{
					if (files::read_dimensions(paths[index], width, height)) found.push_back({index, width, height});
				}
				catch (...)
				{
				}
			}
			platform::queue_ui([weak, generation, paths = std::move(paths), end, found = std::move(found)]() mutable
			{
				const auto self = weak.lock();
				if (!self || generation != self->folderScanGeneration_) return;
				for (const auto& value : found)
				{
					self->collection_.set_dimensions(paths[value.index], value.width, value.height);
					self->canvas_.set_item_dimensions(paths[value.index], value.width, value.height);
				}
				if (end < paths.size()) self->pump_dimensions(generation, std::move(paths), end);
				else if (self->collection_.sort_field() == files::SortField::dimensions)
				{
					self->collection_.resort();
					self->apply_collection_view();
				}
			});
		});
	}

	MainWindow::CommandContext MainWindow::build_command_context() const
	{
		CommandContext context;
		context.selectionCount = canvas_.selected_paths().size();
		const auto clipboard = platform::clipboard_status();
		context.clipboardHasFiles = clipboard.hasFiles;
		context.clipboardHasImage = clipboard.hasImage;
		context.hasFolder = !folder_.empty();
		context.fullscreen = fullscreen_;
		context.taskActive = task_ != nullptr;
		context.focus = canvas_.frame() && canvas_.frame()->has_focus() ? FocusTarget::canvas
		                : shellTree_ && shellTree_->has_focus()         ? FocusTarget::tree
		                                                                : FocusTarget::none;
		// A task owns the surface, so nothing that acts on the Items selection may fire under it.
		if (context.taskActive) context.selectionCount = 0;
		return context;
	}

	void MainWindow::update_toolbar_state()
	{
		if (chrome_) chrome_->refresh();
	}

	void MainWindow::update_collection_controls()
	{
		if (chrome_) chrome_->refresh();
		on_size();
	}

	void MainWindow::update_status_surface()
	{
		if (chrome_) chrome_->set_status({statusText_, progressPercent_, scanning_});
	}

	void MainWindow::update_progress(const size_t complete, const size_t total)
	{
		if (informationProgressActive_) return;
		scanning_ = false;
		progressPercent_ = total ? static_cast<int>((std::min)(complete, total) * 100 / total) : 100;
		update_status_surface();
	}

	void MainWindow::update_information_progress(const size_t complete, const size_t total)
	{
		if (task_) return;
		informationProgressActive_ = total > 0 && complete < total;
		if (!total)
		{
			update_selection_status();
			return;
		}
		progressPercent_ = static_cast<int>((std::min)(complete, total) * 100 / total);
		if (informationProgressActive_)
			statusText_ = std::format(L"Reading file information: {} of {}", complete, total);
		else update_selection_status();
		update_status_surface();
	}

	void MainWindow::open_file_picker()
	{
		const auto path = platform::choose_open_file(
			frame_, {L"Images, video and audio",
		             L"*.bmp;*.dib;*.gif;*.ico;*.jpg;*.jpeg;*.png;*.tif;*.tiff;*.wdp;*.webp;"
		             L"*.avi;*.m4v;*.mkv;*.mov;*.mp4;*.mpeg;*.mpg;*.wmv;*.webm;*.mts;*.m2ts;*.asf;*.3gp;*.3g2;*.ts;"
		             L"*.aac;*.flac;*.m4a;*.mp3;*.ogg;*.wav;*.wma;*.opus;*.aif;*.aiff"});
		if (!path) return;
		open_folder(path->parent_path(), true, true, *path);
	}

	void MainWindow::show_about()
	{
		platform::show_modal_dialog(frame_, about_dialog());
	}

	platform::DialogDefinition MainWindow::about_dialog()
	{
		// The dialog refuses duplicate field ids, so each link needs its own.
		platform::DialogField website;
		website.id = 1;
		website.kind = platform::DialogFieldKind::action;
		website.label = L"&Visit https://imagewalker.com/";
		website.invoke = []
		{
			platform::open_uri(L"https://imagewalker.com/");
		};
		platform::DialogField song;
		song.id = 2;
		song.kind = platform::DialogFieldKind::action;
		song.label = L"&Theme song: \"Ordinary\" by Alex Warren";
		song.invoke = []
		{
			platform::open_uri(L"https://www.youtube.com/watch?v=u2ah9tWTkmk");
		};
		platform::DialogDefinition definition;
		definition.title = L"About ImageWalker";
		definition.message = L"ImageWalker 3.0 (2026)\nA lightweight Windows photo browser and editor.\n"
							 L"Copyright (C) Zac Walker";
		definition.fields.push_back(std::move(website));
		definition.fields.push_back(std::move(song));
		definition.acceptText = L"Close";
		definition.cancelText.clear();
		return definition;
	}

	void MainWindow::update_breadcrumb()
	{
		breadcrumbParts_.clear();
		std::filesystem::path current = folder_.root_path();
		if (!current.empty()) breadcrumbParts_.push_back({current, current.wstring()});
		for (const auto& component : folder_.relative_path())
		{
			current /= component;
			breadcrumbParts_.push_back({current, component.wstring()});
		}
		if (chrome_) chrome_->set_breadcrumbs(breadcrumbParts_, [this](const auto& path) { open_folder(path); });
		update_toolbar_state();
	}

	void MainWindow::navigate_history(const int direction)
	{
		const auto target = history_.go(direction);
		if (!target) return;
		// A remembered folder can disappear; drop it rather than leaving Back and Forward lying.
		if (!open_folder(*target, false))
		{
			history_.rollback();
			history_.forget(*target);
			update_toolbar_state();
		}
	}

	void MainWindow::set_fullscreen(const bool fullscreen)
	{
		if (fullscreen == fullscreen_) return;
		fullscreen_ = fullscreen;
		canvas_.set_fullscreen(fullscreen);
		frame_->set_fullscreen(fullscreen);
		update_visibility();
	}

	void MainWindow::update_fullscreen_menu_checks()
	{
		if (chrome_) chrome_->refresh();
	}

	void MainWindow::update_visibility()
	{
		const bool showTreePane = !fullscreen_ && !zoomMode_ && showTree_ && !task_;
		if (shellTree_) shellTree_->show(showTreePane);
		on_size();
	}

	void MainWindow::show_options()
	{
		platform::DialogField format;
		format.id = dialogPasteImageFormat;
		format.kind = platform::DialogFieldKind::choice;
		format.label = L"Save pasted images &as:";
		format.value = static_cast<std::int64_t>(pasteImageFormat_);
		for (const auto value : files::writable_image_formats())
		{
			format.choices.push_back({files::image_save_format_name(value), static_cast<std::int64_t>(value)});
		}

		platform::DialogDefinition definition;
		definition.title = L"Options";
		definition.message =
			L"Choose the file format used when a clipboard image is pasted into the current folder as a new image file.";
		definition.fields.push_back(std::move(format));
		definition.validate = [](const platform::DialogValues& values) -> std::optional<std::wstring>
		{
			const auto found = values.find(dialogPasteImageFormat);
			return found != values.end() && std::get_if<std::int64_t>(&found->second)
			           ? std::nullopt
			           : std::optional<std::wstring>(L"Select an image format.");
		};
		const auto values = platform::show_modal_dialog(frame_, std::move(definition));
		if (!values) return;
		const auto found = values->find(dialogPasteImageFormat);
		if (found == values->end()) return;
		if (const auto selected = std::get_if<std::int64_t>(&found->second))
		{
			pasteImageFormat_ = static_cast<files::ImageSaveFormat>(*selected);
			save_settings();
		}
	}

	void MainWindow::layout_center()
	{
		if (task_) task_->set_bounds(centerBounds_);
		else if (canvas_.frame()) canvas_.frame()->move(centerBounds_);
	}

	void MainWindow::set_view_mode(const ViewMode mode)
	{
		viewMode_ = mode;
		const auto canvasMode = mode == ViewMode::details      ? ImageCanvas::ItemMode::details
		                        : mode == ViewMode::thumbnails ? ImageCanvas::ItemMode::thumbnails
		                                                       : ImageCanvas::ItemMode::matrix;
		canvas_.set_mode(canvasMode);
		if (chrome_) chrome_->refresh();
		layout_center();
		probe_dimensions();
	}

	void MainWindow::set_thumbnail_size(const int size)
	{
		const int value = std::clamp(size, 64, 256);
		if (thumbnailSize_ == value && canvas_.thumbnail_size() == value) return;
		thumbnailSize_ = value;
		canvas_.set_thumbnail_size(thumbnailSize_);
		if (chrome_)
		{
			chrome_->set_thumbnail_size(thumbnailSize_, [this](const int next) { set_thumbnail_size(next); });
			chrome_->refresh();
		}
	}

	void MainWindow::update_selection_status()
	{
		if (task_) return;
		if (collection_.load_state() == LoadState::failed)
		{
			const auto message = collection_.load_error().message();
			statusText_ = std::format(L"Could not read {}: {}", folder_.wstring(),
			                          std::wstring(message.begin(), message.end()));
			update_status_surface();
			return;
		}
		update_mode_bar();
		const auto selected = canvas_.selected_paths().size();
		size_t images = 0;
		size_t folders = 0;
		for (const auto& item : collection_.visible_items())
		{
			if (item.kind == files::ItemKind::image) ++images;
			else if (item.kind == files::ItemKind::folder) ++folders;
		}
		const size_t otherFiles = collection_.visible_items().size() - images - folders;
		statusText_ = selected ? format::selection_count(selected, images, folders, otherFiles)
		                       : format::item_count(images, folders, otherFiles);
		update_status_surface();
	}

	void MainWindow::load_settings()
	{
		constexpr std::wstring_view section = L"ImageWalker30";
		const auto read = [section](const std::wstring_view key, const int fallback)
		{
			return platform::read_integer_setting(section, key, fallback);
		};
		constexpr int missing = (std::numeric_limits<int>::min)();
		const int left = read(L"WindowLeft", missing);
		const int top = read(L"WindowTop", missing);
		const int width = (std::max)(640, read(L"WindowWidth", 1280));
		const int height = (std::max)(480, read(L"WindowHeight", 800));
		hasSavedWindowPosition_ = left != missing && top != missing;
		savedWindowBounds_ = {hasSavedWindowPosition_ ? left : 0, hasSavedWindowPosition_ ? top : 0, width, height};
		startMaximized_ = read(L"Maximized", 0) != 0;
		showTree_ = read(L"ShowFolderTree", 1) != 0;
		folderTreeWidth_ = std::clamp(read(L"FolderTreeWidth", 230), 140, 1000);
		fitImage_ = read(L"FitImage", 1) != 0;
		savedSplitterPosition_ = read(L"SplitterPosition", -1);
		const int savedViewMode = read(L"ViewMode", 1);
		viewMode_ = savedViewMode >= 0 && savedViewMode <= static_cast<int>(ViewMode::matrix)
		                ? static_cast<ViewMode>(savedViewMode)
		                : ViewMode::thumbnails;
		thumbnailSize_ = std::clamp(read(L"ThumbnailSize", 128), 64, 256);
		pasteImageFormat_ = static_cast<files::ImageSaveFormat>(
			std::clamp(read(L"PasteImageFormat", 0), 0, static_cast<int>(files::ImageSaveFormat::webp)));
		collection_.set_photos_only(read(L"PhotosOnly", 0) != 0);
		collection_.set_sort_field(static_cast<files::SortField>(std::clamp(read(L"SortField", 0), 0, 5)));
		showModeBar_ = read(L"ShowModeBar", 1) != 0;
		openDestinationAfterTransfer_ = read(L"OpenDestinationAfterTransfer", 0) != 0;
		lastTransferFolder_ = platform::read_text_setting(section, L"LastTransferFolder");
		lastFolder_ = platform::read_text_setting(section, L"LastFolder");
	}

	void MainWindow::save_settings() const
	{
		if (!frame_) return;
		const auto placement = frame_->placement();
		const std::array values{platform::IntegerSetting{L"WindowLeft", placement.normalBounds.x},
		                        platform::IntegerSetting{L"WindowTop", placement.normalBounds.y},
		                        platform::IntegerSetting{L"WindowWidth", placement.normalBounds.width},
		                        platform::IntegerSetting{L"WindowHeight", placement.normalBounds.height},
		                        platform::IntegerSetting{L"Maximized", placement.maximized},
		                        platform::IntegerSetting{L"ShowFolderTree", showTree_},
		                        platform::IntegerSetting{L"FolderTreeWidth", folderTreeWidth_},
		                        platform::IntegerSetting{L"FitImage", canvas_.fit()},
		                        platform::IntegerSetting{L"ViewMode", static_cast<int>(viewMode_)},
		                        platform::IntegerSetting{L"ThumbnailSize", thumbnailSize_},
		                        platform::IntegerSetting{L"PasteImageFormat", static_cast<int>(pasteImageFormat_)},
		                        platform::IntegerSetting{L"PhotosOnly", collection_.photos_only()},
		                        platform::IntegerSetting{L"SortField", static_cast<int>(collection_.sort_field())},
		                        platform::IntegerSetting{L"SplitterPosition", canvas_.splitter_position()},
		                        platform::IntegerSetting{L"ShowModeBar", showModeBar_},
		                        platform::IntegerSetting{L"OpenDestinationAfterTransfer",
		                                                 openDestinationAfterTransfer_}};
		platform::write_integer_settings(L"ImageWalker30", values);
		const std::array text{platform::TextSetting{L"LastTransferFolder", lastTransferFolder_},
		                      platform::TextSetting{L"LastFolder", lastFolder_.wstring()}};
		platform::write_text_settings(L"ImageWalker30", text);
	}
}
