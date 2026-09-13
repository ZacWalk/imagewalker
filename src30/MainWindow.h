// ImageWalker by Zac Walker
// Declares application state and coordinates platform-owned chrome, navigation, and image canvas surfaces.

#pragma once

#include "Platform.h"
#include "CollectionState.h"
#include "ImageCanvas.h"
#include "ModeBar.h"
#include "NavigationHistory.h"
#include "TaskView.h"
#include "Tools.h"
#include <filesystem>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace iw
{
	namespace tests { class MainWindowAccess; }
	namespace undo { class History; }

	class MainWindow final : public platform::FrameReactor, public std::enable_shared_from_this<MainWindow>
	{
	public:
		// An injected history must outlive the window; an absent service disables file Undo.
		explicit MainWindow(undo::History* history = nullptr) : undoHistory_(history) {}
		bool create(int showCommand, const std::filesystem::path& initialPath);
		const platform::WindowFramePtr& frame() const { return frame_; }

	private:
		friend class tests::MainWindowAccess;

		enum class ViewMode { details, thumbnails, matrix };
		enum class FocusTarget { none, canvas, tree };

		// Mode-bar button ids. Browse is the way back out of every task.
		enum ModeId
		{
			modeBrowse = 1,
			modeFolders,
			modeConvert,
			modeRename,
			modeEdit,
			modeSync,
			modeFullscreen,
			shortcutFirst = 100
		};

		struct CommandContext
		{
			size_t selectionCount{};
			bool clipboardHasFiles{};
			bool clipboardHasImage{};
			bool hasFolder{};
			bool fullscreen{};
			bool taskActive{};
			FocusTarget focus{FocusTarget::canvas};
		};

		platform::MessageResult message(const platform::WindowFramePtr& frame,
		                                platform::WindowMessage message) override;
		platform::MessageResult mouse(const platform::WindowFramePtr& frame, platform::MouseMessage message,
		                              const platform::MouseInput& input) override;
		void paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw) override;
		void size(const platform::WindowFramePtr& frame, sizei extent,
		          platform::MeasureContext& measure) override;
		bool on_create();
		void on_size();
		void update_dpi(unsigned int dpi);
		int scale_metric(int value) const;
		void configure_chrome();
		void update_breadcrumb();
		void update_toolbar_state();
		CommandContext build_command_context() const;
		void update_progress(size_t complete, size_t total);
		void update_information_progress(size_t complete, size_t total);
		void update_status_surface();
		void open_file_picker();
		void open_folder_picker();
		bool open_folder(const std::filesystem::path& path, bool addHistory = true, bool synchronizeTree = true);
		void report_scan_progress(std::uint64_t generation, size_t entries, size_t images);
		void finish_folder_scan(std::uint64_t generation, const std::filesystem::path& folder,
		                        std::vector<files::FolderItem> items);
		void open_image(const std::filesystem::path& path);
		void activate_item(const std::filesystem::path& path);
		void apply_collection_view();
		void set_sort_field(files::SortField field);
		void update_collection_controls();
		void navigate_history(int direction);
		void set_fullscreen(bool fullscreen);
		void update_fullscreen_menu_checks();
		void update_visibility();
		void set_view_mode(ViewMode mode);
		void set_thumbnail_size(int size);
		void layout_center();
		std::vector<std::filesystem::path> selected_paths() const;
		void update_selection_status();
		void show_file_menu(pointi screenPoint);
		void rename_selected();
		void delete_selected();
		bool can_undo_last_action() const;
		void undo_last_action();
		void copy_selected(bool cut);
		void paste_files();
		void import_dropped_files(const std::vector<std::filesystem::path>& paths);
		void show_options();
		void show_about();
		void load_settings();
		void save_settings() const;

		// MainWindowTasks.cpp
		void configure_mode_bar();
		void update_mode_bar();
		void mode_bar_command(const ModeBar::Button& button);
		void remove_shortcut(const ModeBar::Button& button);
		void load_shortcuts();
		void save_shortcuts() const;
		bool set_task(TaskViewPtr task);
		bool close_task();
		void request_exit();
		bool task_active() const { return task_ != nullptr; }
		void open_convert_task();
		void open_rename_task();
		void open_sync_task();
		void open_edit_task();
		bool has_photo_selection() const;
		std::vector<std::filesystem::path> photo_selection() const;
		std::filesystem::path editable_photo() const;

		// MainWindowFiles.cpp
		void copy_or_move_to_folder(bool move);
		void show_open_with_menu(pointi screenPoint);
		void open_with_chooser();

		platform::WindowFramePtr frame_;
		platform::CommandSurfacePtr chrome_;
		platform::WindowFramePtr contentFrame_;
		platform::ShellTreePtr shellTree_;
		platform::WindowFramePtr treeSplitterFrame_;
		platform::WindowFramePtr centerFrame_;
		ModeBarPtr modeBar_;
		TaskViewPtr task_;
		bool closeApplicationAfterTask_{};
		tools::ToolTable tools_;
		std::vector<std::filesystem::path> shortcuts_;
		ImageCanvas canvas_;
		CollectionState collection_;
		std::vector<std::filesystem::path> files_;
		NavigationHistory history_;
		undo::History* undoHistory_{};

		// Read from the scan queue to cancel a superseded scan, so it stays atomic.
		std::atomic_uint64_t folderScanGeneration_{};
		std::filesystem::path folder_;

		std::vector<platform::Breadcrumb> breadcrumbParts_;
		std::wstring statusText_;
		unsigned int dpi_{96};
		int progressPercent_{100};
		bool scanning_{};
		bool informationProgressActive_{};
		bool synchronizingTree_{};
		bool draggingTreeSplitter_{};
		bool hoveringTreeSplitter_{};
		recti savedWindowBounds_{0, 0, 1280, 800};
		bool hasSavedWindowPosition_{};
		int savedSplitterPosition_{-1};
		int folderTreeWidth_{230};
		int treeSplitterDragOffset_{};
		bool startMaximized_{};
		bool showTree_{true};
		// Seeds the canvas at startup only; the canvas owns fit state from then on.
		bool fitImage_{true};
		bool zoomMode_{};
		bool fullscreen_{};
		int thumbnailSize_{128};
		bool showModeBar_{true};
		std::wstring lastTransferFolder_;
		bool openDestinationAfterTransfer_{};
		files::ImageSaveFormat pasteImageFormat_{files::ImageSaveFormat::png};
		ViewMode viewMode_{ViewMode::thumbnails};
	};
}
