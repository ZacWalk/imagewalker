// ImageWalker by Zac Walker
// Declares the shared task surface: controls panel, review list, progress, cancellation, results.

#pragma once

#include "CanvasRenderer.h"
#include "ControlPanel.h"
#include "Files.h"
#include "Platform.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "util_layout.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iw
{
	// The Items surface is replaced by one of these while a task is open. Concrete views supply
	// columns, controls, an analyse function and a run function; they contain no windowing code.
	class TaskView : public platform::FrameReactor, public std::enable_shared_from_this<TaskView>
	{
	public:
		struct Column
		{
			std::wstring title;
			int weight{1};
		};

		struct Host
		{
			std::function<void()> closed; // return to Items
			std::function<void(std::wstring)> status;
			std::function<void(size_t, size_t)> progress;
			std::function<void()> refreshChrome;
			std::function<void(const tasks::TaskPlan&)> completed;
			std::function<void(const std::filesystem::path&, bool saveAs)> saved;
			std::function<void()> toggleMaximize;
			std::function<bool()> maximized;
		};

		TaskView();
		explicit TaskView(tasks::TaskRunner::Dispatcher dispatcher);
		~TaskView() override;
		TaskView(const TaskView&) = delete;
		TaskView& operator=(const TaskView&) = delete;

		bool create(const platform::WindowFramePtr& parent, Host host);
		platform::WindowFramePtr frame() const { return frame_; }
		void set_dpi(unsigned int dpi);
		void set_bounds(recti bounds);
		void show(bool visible);
		// After request_close succeeds, release both the native child and its reactor ownership.
		bool dispose();

		void set_targets(std::vector<std::filesystem::path> targets);
		const std::vector<std::filesystem::path>& targets() const { return targets_; }

		virtual std::vector<platform::ToolbarItem> toolbar_items();

		void initialize();
		bool analyzes_automatically() const { return uses_plan() && auto_analyze(); }
		void analyze();
		virtual void start_run();
		void cancel();

		// False vetoes the close, which is how a running task keeps itself alive.
		virtual bool request_close();
		virtual bool closing() const { return closePending_; }
		virtual bool can_undo_draft() const { return false; }
		virtual bool undo_draft() { return false; }

		bool busy() const { return analyzing_ || running_ || runner_.busy(); }
		const tasks::TaskPlan& plan() const { return plan_; }

		// The mode-bar button this view belongs to. The host owns the numbering.
		void set_mode_id(const int id) { modeId_ = id; }
		int mode_id() const { return modeId_; }

		virtual std::wstring title() const = 0;

	protected:
		// Concrete views implement these four.
		virtual void build_controls() = 0;
		virtual std::vector<Column> review_columns() const = 0;
		// Built on the UI thread and run on a worker, so it must capture its settings by value.
		virtual tasks::TaskRunner::AnalyzeFunction make_analyzer() = 0;
		virtual tasks::RunOptions build_run_options() = 0;

		virtual std::wstring run_label() const { return L"Run"; }
		virtual std::wstring refresh_label() const { return L"Refresh"; }
		virtual std::wstring run_block_reason() const { return {}; }
		virtual std::wstring analysis_block_reason() const { return {}; }
		virtual std::wstring analysis_summary() const;
		// False means the user asks for analysis explicitly, because scanning is substantial work.
		virtual bool auto_analyze() const { return true; }
		virtual std::wstring description() const { return {}; }
		virtual std::wstring review_cell(const tasks::TaskRow& row, size_t column) const;
		// Positions any native control the view hosts inside the drawn panel.
		virtual void layout_hosted_controls()
		{
		}

		// False means the view has no reviewed plan, so no analysis and no runner.
		virtual bool uses_plan() const { return true; }

		// The area beside the controls panel. The default draws the review list.
		virtual void draw_content(const ui::CanvasRenderer& renderer, recti bounds);
		virtual bool content_mouse(platform::MouseMessage message, const platform::MouseInput& input);
		virtual bool content_key(const platform::KeyInput& input);
		virtual bool content_dragging() const { return false; }
		// The panel raises anything needing a window; the base handles folder pickers and choices.
		virtual void control_activated(ui::Control& control);
		virtual void controls_changed();
		virtual void work_state_changed(bool) {}
		virtual bool confirm_cancel_close();
		bool control_key(const platform::KeyInput& input);
		void bind_text_input(int controlId, platform::TextInputOptions& options);
		void append_window_commands(std::vector<platform::ToolbarItem>& items);

		void rebuild_controls();
		void invalidate();
		void refresh_commands();
		void set_status(std::wstring text);
		void ask_host_to_close() const { if (host_.closed) host_.closed(); }
		std::filesystem::path choose_folder(std::wstring_view title) const;
		void show_choice_popup(ui::Control& control);
		int metric(const int value) const { return ui::scale_metric(value, dpi_); }

		ui::ControlPanel panel_;
		tasks::TaskPlan plan_;
		std::vector<std::filesystem::path> targets_;
		platform::WindowFramePtr frame_;
		Host host_;
		unsigned int dpi_{96};
		bool finished_{};
		int modeId_{};
		recti contentBounds_;

	private:
		struct Thumbnail
		{
			int width{};
			int height{};
			std::vector<std::uint32_t> pixels;
		};

		platform::MessageResult message(const platform::WindowFramePtr& frame,
		                                platform::WindowMessage message) override;
		platform::MessageResult mouse(const platform::WindowFramePtr& frame, platform::MouseMessage message,
		                              const platform::MouseInput& input) override;
		platform::MessageResult key(const platform::WindowFramePtr& frame, platform::KeyMessage message,
		                            const platform::KeyInput& input) override;
		void paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw) override;
		void size(const platform::WindowFramePtr& frame, sizei extent,
		          platform::MeasureContext& measure) override;

		void layout(const ui::CanvasRenderer& renderer);
		void draw_panel(const ui::CanvasRenderer& renderer) const;
		void draw_review(const ui::CanvasRenderer& renderer) const;
		void draw_status(const ui::CanvasRenderer& renderer) const;
		void ensure_row_visible(size_t row);
		void request_thumbnails();
		void update_command_state();
		void complete_pending_close();
		int row_height() const;

		tasks::TaskRunner runner_;
		ui::VerticalScroll reviewScroll_;
		ui::VerticalScroll panelScroll_;
		std::vector<Thumbnail> thumbnails_;
		std::uint64_t thumbnailGeneration_{};
		recti panelBounds_;
		recti thumbnailBounds_;
		recti reviewBounds_;
		recti reviewHeaderBounds_;
		recti statusBounds_;
		std::wstring statusText_;
		std::vector<Column> columns_;
		size_t runningRow_{};
		size_t completed_{};
		size_t total_{};
		bool analyzing_{};
		bool running_{};
		bool cancelling_{};
		bool closePending_{};
		bool closePrompt_{};
		bool optionsChangedDuringRun_{};
		bool trackingMouse_{};
		bool panelCapture_{};
		ui::VerticalScroll* draggedScroll_{};
		int scrollGrab_{};
		platform::CommandPtr runCommand_;
		platform::CommandPtr cancelCommand_;
		platform::CommandPtr refreshCommand_;
		platform::CommandPtr closeCommand_;
	};

	using TaskViewPtr = std::shared_ptr<TaskView>;
}
