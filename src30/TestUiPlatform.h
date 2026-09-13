// Recording implementations of the platform boundary, not replacements for UI behavior.
#pragma once

#include "MainWindow.h"

namespace iw::tests
{
	class RecordingFrame final : public platform::WindowFrame,
		public std::enable_shared_from_this<RecordingFrame>
	{
	public:
		platform::FrameReactorPtr reactor;
		std::vector<std::shared_ptr<RecordingFrame>> children;
		recti bounds{0, 0, 1000, 700};
		std::wstring title;
		bool visible{true};
		bool focused{};
		bool fullscreen{};
		bool maximized{};
		bool refuseChild{};
		int invalidations{};
		int tracks{};
		int closes{};
		int fullscreenChanges{};

		platform::NativeHandle native_handle() const override { return {}; }
		void set_reactor(platform::FrameReactorPtr value) override { reactor = std::move(value); }
		platform::WindowFramePtr create_child(platform::FrameReactorPtr value,
			const platform::WindowOptions& options) override
		{
			if (refuseChild) return {};
			auto child = std::make_shared<RecordingFrame>();
			child->reactor = std::move(value);
			child->visible = options.visible;
			children.push_back(child);
			return child;
		}
		recti client_rect() const override { return {0, 0, bounds.width, bounds.height}; }
		void move(recti value) override { bounds = value; }
		void show(bool value) override { visible = value; }
		void invalidate() override { ++invalidations; }
		void invalidate(recti) override { ++invalidations; }
		void set_focus() override { focused = true; }
		bool has_focus() const override { return focused; }
		void set_capture() override {}
		void release_capture() override {}
		void track_mouse_leave() override { ++tracks; }
		void configure_gestures(bool, bool) override {}
		void start_timer(unsigned int) override {}
		void stop_timer() override {}
		void accept_file_drops(bool) override {}
		void set_cursor(platform::CursorShape) override {}
		pointi screen_to_client(pointi value) const override { return value; }
		pointi client_to_screen(pointi value) const override { return value; }
		void set_title(std::wstring_view value) override { title = value; }
		void set_fullscreen(bool value) override { fullscreen = value; ++fullscreenChanges; }
		void set_maximized(bool value) override { maximized = value; }
		platform::WindowPlacement placement() const override { return {bounds, maximized}; }
		void set_accelerators(std::span<const platform::CommandAccelerator>) override {}
		void close() override { ++closes; }
		unsigned int dpi() const override { return 96; }

		void mouse(platform::MouseMessage message, pointi point)
		{
			if (reactor)
			{
				const auto keepAlive = reactor;
				platform::MouseInput input;
				input.point = point;
				keepAlive->mouse(shared_from_this(), message, input);
			}
		}
		void release_tree()
		{
			for (const auto& child : children) child->release_tree();
			children.clear();
			reactor.reset();
		}
	};

	class RecordingChrome final : public platform::CommandSurface
	{
	public:
		std::vector<platform::ToolbarItem> taskToolbar;
		platform::StatusInfo status;
		bool fullscreenLayout{};
		int refreshes{};
		int clears{};
		void set_menu(std::vector<platform::Menu>) override {}
		void set_toolbar(std::vector<platform::ToolbarItem>) override {}
		void set_task_toolbar(std::vector<platform::ToolbarItem> value) override { taskToolbar = std::move(value); }
		void clear_task_toolbar() override { taskToolbar.clear(); ++clears; }
		void set_status_toolbar(std::vector<platform::ToolbarItem>) override {}
		void set_breadcrumbs(std::vector<platform::Breadcrumb>,
			std::function<void(const std::filesystem::path&)>) override {}
		void set_status(platform::StatusInfo value) override { status = std::move(value); }
		void set_thumbnail_size(int, std::function<void(int)>) override {}
		recti layout(recti client, bool fullscreen) override { fullscreenLayout = fullscreen; return client; }
		void set_dpi(unsigned int) override {}
		void refresh() override { ++refreshes; }
	};

	class RecordingTree final : public platform::ShellTree
	{
	public:
		recti bounds;
		bool visible{true};
		void set_bounds(recti value) override { bounds = value; }
		void show(bool value) override { visible = value; }
		void set_focus() override {}
		bool has_focus() const override { return false; }
		void set_current_path(const std::filesystem::path&) override {}
		void refresh() override {}
	};

	// Bypass settings, shell enumeration and the OS event loop; all switching, layout,
	// command handling and TaskView::Host callbacks below are MainWindow's implementation.
	class MainWindowAccess
	{
	public:
		explicit MainWindowAccess(undo::History* history = nullptr) : window(std::make_shared<MainWindow>(history)) {}
		std::shared_ptr<MainWindow> window;
		std::shared_ptr<RecordingFrame> root = std::make_shared<RecordingFrame>();
		std::shared_ptr<RecordingChrome> chrome = std::make_shared<RecordingChrome>();
		std::shared_ptr<RecordingTree> tree = std::make_shared<RecordingTree>();

		bool create()
		{
			window->frame_ = root;
			window->chrome_ = chrome;
			window->shellTree_ = tree;
			window->contentFrame_ = root->create_child({}, {});
			window->treeSplitterFrame_ = window->contentFrame_->create_child({}, {});
			window->centerFrame_ = window->contentFrame_->create_child({}, {});
			if (!window->canvas_.create(window->centerFrame_)) return false;
			window->modeBar_ = std::make_shared<ModeBar>();
			if (!window->modeBar_->create(window->contentFrame_)) return false;
			window->configure_mode_bar();
			window->update_visibility();
			return true;
		}
		~MainWindowAccess() { root->release_tree(); }

		static std::shared_ptr<RecordingFrame> record(const platform::WindowFramePtr& frame)
		{
			return std::static_pointer_cast<RecordingFrame>(frame);
		}
		std::shared_ptr<RecordingFrame> canvas() const { return record(window->canvas_.frame()); }
		std::shared_ptr<RecordingFrame> center() const { return record(window->centerFrame_); }
		std::shared_ptr<RecordingFrame> modes() const { return record(window->modeBar_->frame()); }
		std::shared_ptr<RecordingFrame> splitter() const { return record(window->treeSplitterFrame_); }
		bool set_task(TaskViewPtr task) { return window->set_task(std::move(task)); }
		TaskViewPtr task() const { return window->task_; }
		void browse() { window->mode_bar_command({MainWindow::modeBrowse}); }
		void folders() { window->mode_bar_command({MainWindow::modeFolders}); }
		void fullscreen() { window->mode_bar_command({MainWindow::modeFullscreen}); }
		void set_fullscreen(bool value) { window->set_fullscreen(value); }
		void exit() { window->request_exit(); }
		void resize(recti value) { root->move(value); window->on_size(); }
		void information_progress() { window->update_information_progress(1, 2); }
		bool fullscreen_state() const { return window->fullscreen_; }
		bool can_undo() const { return window->can_undo_last_action(); }
		void undo() { window->undo_last_action(); }
	};
}
